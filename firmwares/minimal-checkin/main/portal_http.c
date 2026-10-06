// main/portal_http.c —— 配网门户实现：手写 DNS + HTTP，零依赖 lwIP socket
//
// 设计要点：
//  - DNS(UDP 53): 所有 A 查询都答 192.168.4.1 → 手机连热点后弹"登录到网络"
//  - HTTP(TCP 80): 单线程顺序 accept。AP 模式最多 2 个连接，简单可靠。
//  - 扫描在 STA 接口进行（APSTA 模式下 esp_wifi_scan_start 可用）。
#include "portal_http.h"
#include "wifi_manager.h"
#include "checkin_store.h"
#include "app_config.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>

static const char *TAG = "portal";

#define PORTAL_PORT      80
#define DNS_PORT         53
#define AP_IP            "192.168.4.1"
#define RX_BUF_SIZE      2048
#define HTTP_TIMEOUT_MS  3000
#define MAX_RESPONSE     (8 * 1024)

static char s_ap_name[24];
static volatile bool s_running;
static TaskHandle_t s_dns_task;
static TaskHandle_t s_http_task;

// ---------------- DNS ----------------
// 只处理标准 A 查询；把应答 IP 写成 192.168.4.1
static void dns_task_fn(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { vTaskDelete(NULL); return; }

    struct sockaddr_in bind_addr = { 0 };
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(DNS_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    uint8_t req[512];
    while (s_running) {
        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);
        int n = recvfrom(sock, req, sizeof(req), 0,
                         (struct sockaddr *)&src, &src_len);
        if (n < 12) continue;   // DNS 头都不够
        if ((req[2] & 0x80) != 0) continue;   // 不是查询包

        // 应答：复用请求头部，问题段原样保留，答案段接在后面
        uint8_t resp[512];
        int qlen = n;          // 简化：整个请求视为问题段（无附加段）
        memcpy(resp, req, qlen);
        // 置 QR=1, AA=1, RA=1, RCODE=0
        resp[2] = 0x85;
        resp[3] = 0x00;
        int ancount = 1;
        resp[6] = 0; resp[7] = (uint8_t)ancount;
        // ARCOUNT=0
        resp[10] = 0; resp[11] = 0;

        // 答案段：CNAME 指针 0xC00C（指向第 12 字节起的问题名）+ A + 1.2.3.4
        int off = qlen;
        uint8_t ans[] = { 0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01,
                          0x00, 0x00, 0x00, 0x3C, 0x00, 0x04, 192, 168, 4, 1 };
        memcpy(resp + off, ans, sizeof(ans));
        off += (int)sizeof(ans);

        sendto(sock, resp, off, 0, (struct sockaddr *)&src, src_len);
    }
    close(sock);
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

// ---------------- HTTP 工具 ----------------

// 从表单编码串中提取字段值（+ → 空格，%XX 解码）
static bool form_get(const char *body, const char *key, char *out, size_t out_len)
{
    size_t klen = strlen(key);
    const char *p = body;
    while ((p = strstr(p, key)) != NULL) {
        // 键必须是字段开头（前面是 & 或行首），后面紧跟 '='
        if ((p == body || p[-1] == '&') && p[klen] == '=') {
            p += klen + 1;
            size_t o = 0;
            while (*p && *p != '&' && o + 1 < out_len) {
                if (*p == '+') {
                    out[o++] = ' ';
                    p++;
                } else if (*p == '%' && p[1] && p[2]) {
                    int hi = p[1], lo = p[2];
                    int h = (hi >= '0' && hi <= '9') ? hi - '0'
                          : (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10
                          : (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10 : -1;
                    int l = (lo >= '0' && lo <= '9') ? lo - '0'
                          : (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10
                          : (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10 : -1;
                    if (h >= 0 && l >= 0) {
                        out[o++] = (char)((h << 4) | l);
                        p += 3;
                    } else {
                        out[o++] = *p++;
                    }
                } else {
                    out[o++] = *p++;
                }
            }
            out[o] = '\0';
            return true;
        }
        p += klen;
    }
    return false;
}

static void send_all(int sock, const char *data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int n = send(sock, data + sent, len - sent, 0);
        if (n <= 0) break;
        sent += (size_t)n;
    }
}

static void send_response(int sock, int code, const char *type,
                          const char *body, size_t body_len)
{
    static char header[160];
    const char *status = code == 200 ? "OK"
                       : code == 302 ? "Found"
                       : "Bad Request";
    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %u\r\n"
             "Cache-Control: no-store\r\n"
             "Connection: close\r\n"
             "\r\n",
             code, status, type, (unsigned)body_len);
    send_all(sock, header, strlen(header));
    if (body && body_len) send_all(sock, body, body_len);
}

static void redirect_to_self(int sock)
{
    // 302 到固定 IP —— 简单起见不做 DNS 重定向页
    static const char body[] = "<a href='http://" AP_IP "/'>点此打开配网页</a>";
    static char hdr[128];
    snprintf(hdr, sizeof(hdr),
             "HTTP/1.1 302 Found\r\nLocation: http://" AP_IP "/\r\n"
             "Content-Length: %u\r\nConnection: close\r\n\r\n",
             (unsigned)(sizeof(body) - 1));
    send_all(sock, hdr, strlen(hdr));
    send_all(sock, body, sizeof(body) - 1);
}

// ---------------- 页面 ----------------

static const char *PORTAL_HTML_HEAD =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>极简打卡 · 配网</title><style>"
    "body{font-family:sans-serif;background:#f4f0e6;color:#3a3f47;"
    "max-width:420px;margin:0 auto;padding:16px}"
    "h1{font-size:20px}h2{font-size:16px;margin:18px 0 8px}"
    ".card{background:#fff;border-radius:12px;padding:14px;margin:10px 0;"
    "box-shadow:0 2px 6px rgba(0,0,0,.12)}"
    "input{width:100%%;box-sizing:border-box;padding:10px;margin:6px 0;"
    "border:1px solid #d8d2c4;border-radius:8px;font-size:15px}"
    "button{width:100%%;padding:12px;margin-top:8px;border:0;border-radius:8px;"
    "background:#58a55c;color:#fff;font-size:16px}"
    "#nets{max-height:40vh;overflow-y:auto}"
    "#nets div{padding:10px;border-bottom:1px solid #eee;display:flex;"
    "justify-content:space-between;cursor:pointer}"
    "</style></head><body><h1>极简打卡 · 配网</h1>";

// 门户页由多个片段拼装，避免巨型字符串常量
static void send_portal_page(int sock)
{
    char *page = malloc(MAX_RESPONSE);
    if (!page) {
        send_response(sock, 500, "text/plain", "no mem", 6);
        return;
    }
    int off = 0;
    off += snprintf(page + off, MAX_RESPONSE - off, "%s", PORTAL_HTML_HEAD);

    off += snprintf(page + off, MAX_RESPONSE - off,
        "<div class='card'><h2>1. 选择 WiFi</h2>"
        "<div id='nets'>加载中…</div>"
        "<script>fetch('/scan').then(r=>r.json()).then(j=>{"
        "var n=document.getElementById('nets');n.innerHTML='';"
        "j.forEach(a=>{var d=document.createElement('div');"
        "d.innerHTML='<span>'+(a.s==''?'(隐藏网络)':a.s)+'</span><span>信号 '+a.r+'</span>';"
        "d.onclick=()=>{document.getElementById('ssid').value=a.s;"
        "document.getElementById('pass').focus()};"
        "n.appendChild(d)})});</script>"
        "<input id='ssid' placeholder='WiFi 名称'>"
        "<input id='pass' type='password' placeholder='WiFi 密码'>"
        "<button onclick=\"save()\">保存并连接</button>"
        "<div id='msg' style='color:#d96357'></div>"
        "<script>function save(){"
        "var s=document.getElementById('ssid').value;"
        "var p=document.getElementById('pass').value;"
        "if(!s){document.getElementById('msg').textContent='请输入 WiFi 名称';return}"
        "fetch('/save',{method:'POST',body:'ssid='+encodeURIComponent(s)"
        "+'&password='+encodeURIComponent(p)});"
        "document.getElementById('msg').style.color='#58a55c';"
        "document.getElementById('msg').textContent='已提交，设备正在连接…'"
        "}</script></div>");

    card_info_t card;
    checkin_store_get_card(&card);

    off += snprintf(page + off, MAX_RESPONSE - off,
        "<div class='card'><h2>2. 休眠名片（可选）</h2>"
        "<input id='cn' value='%s' placeholder='名称' maxlength='15'>"
        "<input id='ct' value='%s' placeholder='职位' maxlength='15'>"
        "<input id='cb' value='%s' placeholder='个人介绍' maxlength='45'>"
        "<button onclick=\"savecard()\">保存名片</button>"
        "<div id='cmsg' style='color:#58a55c'></div>"
        "<script>function savecard(){"
        "fetch('/card',{method:'POST',body:"
        "'name='+encodeURIComponent(document.getElementById('cn').value)"
        "+'&title='+encodeURIComponent(document.getElementById('ct').value)"
        "+'&bio='+encodeURIComponent(document.getElementById('cb').value)});"
        "document.getElementById('cmsg').textContent='已保存 ✓';"
        "}</script></div>",
        card.name, card.title, card.bio);

    // ---- 第 3 卡：打卡事项（每行一项，最多 6 项；主页弹窗按此列表选择） ----
    off += snprintf(page + off, MAX_RESPONSE - off,
        "<div class='card'><h2>3. 打卡事项</h2><div id='ilist'></div>"
        "<button onclick=\"additem()\" style='background:#8a8578'>＋ 添加一行</button>"
        "<button onclick=\"saveitems()\">保存打卡事项</button>"
        "<div id='imsg' style='color:#d96357'></div>"
        "<script>"
        "function additem(){var l=document.getElementById('ilist');"
        "if(l.getElementsByClassName('item').length>=6)return;"
        "var i=document.createElement('input');i.className='item';"
        "i.maxLength=15;i.placeholder='打卡事项（5个汉字内）';l.appendChild(i)}"
        "function saveitems(){var es=document.getElementById('ilist')"
        ".getElementsByClassName('item');var a=[];"
        "for(var k=0;k<es.length;k++){var v=es[k].value.trim();if(v)a.push(v)}"
        "if(!a.length){document.getElementById('imsg').textContent='至少填写一个事项';return}"
        "fetch('/items',{method:'POST',body:'items='+encodeURIComponent(a.join('\\n'))});"
        "document.getElementById('imsg').style.color='#58a55c';"
        "document.getElementById('imsg').textContent='已保存 ✓'}"
        "fetch('/items').then(function(r){return r.text()}).then(function(t){"
        "var a=t?t.split('\\n'):[];if(!a.length)a=[''];"
        "for(var k=0;k<a.length;k++){additem();"
        "document.getElementById('ilist').getElementsByClassName('item')[k].value=a[k]}})"
        "</script></div>");

    off += snprintf(page + off, MAX_RESPONSE - off,
        "<p style='text-align:center;color:#9a958a;font-size:13px'>"
        "热点：%s · 密码：%s</p></body></html>",
        s_ap_name, APP_AP_PASS_8371);

    (void)off;
    send_response(sock, 200, "text/html; charset=utf-8", page, strlen(page));
    free(page);
}

// ---------------- 请求处理 ----------------

static void handle_scan(int sock)
{
    // 阻塞式扫描：扫完全部信道才返回（约 1.5-2.5s）。
    // 之前非阻塞 + 轮询到首批结果就退出，会漏掉其他信道上的路由器。
    wifi_scan_config_t cfg = {
        .scan_time.active.min = 0,
        .scan_time.active.max = 120,   // 每信道最多驻留 120ms，加快整体扫描
    };
    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    if (err != ESP_OK) {
        send_response(sock, 500, "application/json", "[]", 2);
        return;
    }
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > 30) n = 30;   // 页面够用，省内存
    wifi_ap_record_t *recs = calloc(n, sizeof(wifi_ap_record_t));
    if (!recs) {
        send_response(sock, 200, "application/json", "[]", 2);
        return;
    }
    uint16_t count = n;
    esp_wifi_scan_get_ap_records(&count, recs);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "s", (const char *)recs[i].ssid);
        cJSON_AddNumberToObject(item, "r", recs[i].rssi);
        cJSON_AddItemToArray(arr, item);
    }
    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    free(recs);
    if (!json) {
        send_response(sock, 200, "application/json", "[]", 2);
        return;
    }
    send_response(sock, 200, "application/json", json, strlen(json));
    cJSON_free(json);
}

static void handle_save(int sock, const char *body)
{
    char ssid[33] = { 0 }, pass[65] = { 0 };
    form_get(body, "ssid", ssid, sizeof(ssid));
    form_get(body, "password", pass, sizeof(pass));
    if (!ssid[0]) {
        send_response(sock, 400, "text/plain", "no ssid", 7);
        return;
    }
    ESP_LOGI(TAG, "配网页提交: ssid=%s", ssid);
    wifi_manager_connect(ssid, pass);
    static const char ok[] = "OK";
    send_response(sock, 200, "text/plain", ok, sizeof(ok) - 1);
}

static void handle_card(int sock, const char *body)
{
    char name[APP_CARD_NAME_MAX] = { 0 }, title[APP_CARD_TITLE_MAX] = { 0 },
         bio[APP_CARD_BIO_MAX] = { 0 };
    form_get(body, "name", name, sizeof(name));
    form_get(body, "title", title, sizeof(title));
    form_get(body, "bio", bio, sizeof(bio));
    checkin_store_set_card(name, title, bio);
    static const char ok[] = "OK";
    send_response(sock, 200, "text/plain", ok, sizeof(ok) - 1);
}

// 打卡事项：GET 返回 '\n' 连接的当前列表；POST 保存新列表
static void handle_items_get(int sock)
{
    char buf[CHECKIN_ITEMS_MAX * CHECKIN_ITEM_NAME_MAX + 8];
    int n = checkin_store_get_items_joined(buf, sizeof(buf));
    if (n < 0) n = 0;
    send_response(sock, 200, "text/plain; charset=utf-8", buf, (size_t)n);
}

static void handle_items_post(int sock, const char *body)
{
    char items[CHECKIN_ITEMS_MAX * CHECKIN_ITEM_NAME_MAX + 8] = { 0 };
    form_get(body, "items", items, sizeof(items));
    if (items[0]) checkin_store_set_items(items);
    static const char ok[] = "OK";
    send_response(sock, 200, "text/plain", ok, sizeof(ok) - 1);
}

static void http_task_fn(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { vTaskDelete(NULL); return; }

    struct sockaddr_in bind_addr = { 0 };
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(PORTAL_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0
        || listen(sock, 2) < 0) {
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    char *rx = malloc(RX_BUF_SIZE);
    while (s_running && rx) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int conn = accept(sock, (struct sockaddr *)&cli, &cli_len);
        if (conn < 0) continue;

        // 读超时：手机门户探测请求一般立即到齐
        struct timeval tv = { .tv_sec = HTTP_TIMEOUT_MS / 1000,
                              .tv_usec = (HTTP_TIMEOUT_MS % 1000) * 1000 };
        setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        int n = recv(conn, rx, RX_BUF_SIZE - 1, 0);
        if (n > 0) {
            rx[n] = '\0';
            // 分离头与体
            char *body = strstr(rx, "\r\n\r\n");
            if (body) body += 4;
            bool is_get = strncmp(rx, "GET ", 4) == 0;
            bool is_post = strncmp(rx, "POST ", 5) == 0;
            // Content-Length 截断保护：body 已一次读完，够用

            if (is_post && strncmp(rx, "POST /save", 10) == 0 && body) {
                handle_save(conn, body);
            } else if (is_post && strncmp(rx, "POST /card", 10) == 0 && body) {
                handle_card(conn, body);
            } else if (is_post && strncmp(rx, "POST /items", 11) == 0 && body) {
                handle_items_post(conn, body);
            } else if (is_get && strncmp(rx, "GET /items", 10) == 0) {
                handle_items_get(conn);
            } else if (is_get && strncmp(rx, "GET /scan", 9) == 0) {
                handle_scan(conn);
            } else if (is_get) {
                // 苹果探测（如 /hotspot-detect.html）与任意路径：全部给门户/跳转
                const char *ua = strstr(rx, "User-Agent:");
                bool is_apple_detect = strstr(rx, "hotspot") != NULL
                                    || strstr(rx, "generate_204") != NULL
                                    || strstr(rx, "connecttest") != NULL
                                    || strstr(rx, "kindle") != NULL
                                    || strstr(rx, "success.txt") != NULL;
                if (is_apple_detect && ua) {
                    redirect_to_self(conn);
                } else {
                    send_portal_page(conn);
                }
            } else {
                send_response(conn, 400, "text/plain", "bad", 3);
            }
        }
        close(conn);
    }
    free(rx);
    close(sock);
    s_http_task = NULL;
    vTaskDelete(NULL);
}

// ---------------- 生命周期 ----------------

esp_err_t portal_http_start(const char *ap_name)
{
    if (s_running) return ESP_OK;
    strlcpy(s_ap_name, ap_name ? ap_name : "Checkin", sizeof(s_ap_name));
    s_running = true;

    if (xTaskCreate(dns_task_fn, "portal_dns", 3072, NULL, 4, &s_dns_task) != pdPASS
        || xTaskCreate(http_task_fn, "portal_http", 8192, NULL, 4, &s_http_task) != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "配网门户已启动 (DNS + HTTP)");
    return ESP_OK;
}

esp_err_t portal_http_stop(void)
{
    s_running = false;
    // 任务检测到标志后自行退出并 close socket
    return ESP_OK;
}

bool portal_http_running(void)
{
    return s_running;
}
