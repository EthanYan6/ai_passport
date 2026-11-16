// main/portal_http.c —— 配网门户实现：手写 DNS + HTTP，零依赖 lwIP socket
//
// 设计要点（沿用 minimal-checkin）：
//  - DNS(UDP 53): 所有 A 查询都答 192.168.4.1 → 手机连热点后弹"登录到网络"
//  - HTTP(TCP 80): 单线程顺序 accept。AP 模式最多 2 个连接，简单可靠。
//  - 扫描在 STA 接口进行（APSTA 模式下 esp_wifi_scan_start 可用）。
// 门户四张卡片：WiFi / 火山方舟密钥 / 智谱 API Key / 刷新间隔，
// 分别 POST /save /ark /zcode /ui，保存进 NVS。
#include "portal_http.h"
#include "wifi_manager.h"
#include "usage_store.h"
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
#include <stdlib.h>
#include <errno.h>

static const char *TAG = "portal";

#define PORTAL_PORT      80
#define DNS_PORT         53
#define AP_IP            "192.168.4.1"
#define RX_BUF_SIZE      2048
#define HTTP_TIMEOUT_MS  3000
#define MAX_RESPONSE     (16 * 1024)

static char s_ap_name[24];
static volatile bool s_running;
static TaskHandle_t s_dns_task;
static TaskHandle_t s_http_task;

// ---------------- DNS ----------------
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
        if (n < 12) continue;
        if ((req[2] & 0x80) != 0) continue;

        uint8_t resp[512];
        int qlen = n;
        memcpy(resp, req, qlen);
        resp[2] = 0x85;   // QR=1, AA=1, RA=1, RCODE=0
        resp[3] = 0x00;
        resp[6] = 0; resp[7] = 1;   // ANCOUNT=1
        resp[10] = 0; resp[11] = 0;

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
    "<title>AI 用量胸牌 · 配网</title><style>"
    "body{font-family:sans-serif;background:#0d1420;color:#dfe9f3;"
    "max-width:420px;margin:0 auto;padding:16px}"
    "h1{font-size:20px}h2{font-size:16px;margin:18px 0 8px}"
    ".card{background:#16283a;border-radius:12px;padding:14px;margin:10px 0}"
    "input{width:100%;box-sizing:border-box;padding:10px;margin:6px 0;"
    "border:1px solid #2a4356;border-radius:8px;font-size:15px;"
    "background:#0d1420;color:#dfe9f3}"
    "select{width:100%;padding:10px;margin:6px 0;border-radius:8px;"
    "background:#0d1420;color:#dfe9f3;border:1px solid #2a4356;font-size:15px}"
    "button{width:100%;padding:12px;margin-top:8px;border:0;border-radius:8px;"
    "background:#2fe6ff;color:#062030;font-size:16px}"
    "#nets{max-height:40vh;overflow-y:auto}"
    "#nets div{padding:10px;border-bottom:1px solid #22354a;display:flex;"
    "justify-content:space-between;cursor:pointer}"
    ".msg{color:#39ff88;margin-top:4px;min-height:18px}"
    ".hint{color:#6e8ca0;font-size:13px}"
    "</style></head><body><h1>AI 用量胸牌 · 配网</h1>";

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

    // ---- 卡 1：WiFi ----
    off += snprintf(page + off, MAX_RESPONSE - off,
        "<div class='card'><h2>1. WiFi</h2>"
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
        "<div class='msg' id='msg'></div>"
        "<script>function save(){"
        "var s=document.getElementById('ssid').value;"
        "var p=document.getElementById('pass').value;"
        "if(!s){document.getElementById('msg').textContent='请输入 WiFi 名称';return}"
        "fetch('/save',{method:'POST',body:'ssid='+encodeURIComponent(s)"
        "+'&password='+encodeURIComponent(p)});"
        "var m=document.getElementById('msg');"
        "m.textContent='已提交，设备正在连接…'}</script></div>");

    // ---- 卡 2：火山方舟密钥 ----
    char ak[USAGE_ARK_AK_MAX] = { 0 }, sk[USAGE_ARK_SK_MAX] = { 0 },
         host[USAGE_ARK_HOST_MAX] = { 0 };
    usage_store_get_ark(ak, sizeof(ak), sk, sizeof(sk), host, sizeof(host));
    if (!host[0]) host[0] = '\0';   // 留空用固件默认
    off += snprintf(page + off, MAX_RESPONSE - off,
        "<div class='card'><h2>2. 火山方舟（Coding Plan 用量）</h2>"
        "<input id='ak' value='%s' placeholder='AccessKey (AK)'>"
        "<input id='sk' type='password' value='%s' placeholder='SecretKey (SK)'>"
        "<input id='host' value='%s' placeholder='Host（可留空）'>"
        "<button onclick=\"saveark()\">保存火山密钥</button>"
        "<div class='msg' id='amsg'></div>"
        "<p class='hint'>火山引擎控制台 → 访问控制 IAM → API 访问密钥。"
        "用量接口与 pc-monitor bridge 相同（V4 签名）。</p>"
        "<script>function saveark(){"
        "fetch('/ark',{method:'POST',body:"
        "'ak='+encodeURIComponent(document.getElementById('ak').value)"
        "+'&sk='+encodeURIComponent(document.getElementById('sk').value)"
        "+'&host='+encodeURIComponent(document.getElementById('host').value)});"
        "document.getElementById('amsg').textContent='已保存 ✓'}</script></div>",
        ak, sk, host);

    // ---- 卡 3：智谱 API Key ----
    char zkey[USAGE_ZCODE_KEY_MAX] = { 0 };
    usage_store_get_zcode_key(zkey, sizeof(zkey));
    off += snprintf(page + off, MAX_RESPONSE - off,
        "<div class='card'><h2>3. 智谱 zcode（GLM Coding Plan 用量）</h2>"
        "<input id='zkey' type='password' value='%s' placeholder='API Key'>"
        "<button onclick=\"savezcode()\">保存智谱 Key</button>"
        "<div class='msg' id='zmsg'></div>"
        "<p class='hint'>bigmodel.cn 控制台 → API Key。仅支持国内版。</p>"
        "<script>function savezcode(){"
        "fetch('/zcode',{method:'POST',body:"
        "'apikey='+encodeURIComponent(document.getElementById('zkey').value)});"
        "document.getElementById('zmsg').textContent='已保存 ✓'}</script></div>",
        zkey);

    // ---- 卡 4：刷新间隔 ----
    off += snprintf(page + off, MAX_RESPONSE - off,
        "<div class='card'><h2>4. 刷新间隔</h2><select id='refresh'>");
    uint32_t cur = usage_store_get_refresh_s();
    char opt[96];
    for (size_t i = 0; i < APP_REFRESH_PRESETS_NUM; i++) {
        uint32_t s = APP_REFRESH_PRESETS_S[i];
        if (s >= 3600) {
            snprintf(opt, sizeof(opt),
                     "<option value='%lu'%s>%lu 小时</option>",
                     (unsigned long)s, s == cur ? " selected" : "",
                     (unsigned long)(s / 3600));
        } else if (s >= 60) {
            snprintf(opt, sizeof(opt),
                     "<option value='%lu'%s>%lu 分钟</option>",
                     (unsigned long)s, s == cur ? " selected" : "",
                     (unsigned long)(s / 60));
        } else {
            snprintf(opt, sizeof(opt),
                     "<option value='%lu'%s>%lu 秒</option>",
                     (unsigned long)s, s == cur ? " selected" : "",
                     (unsigned long)s);
        }
        off += snprintf(page + off, MAX_RESPONSE - off, "%s", opt);
    }
    off += snprintf(page + off, MAX_RESPONSE - off,
        "</select><button onclick=\"saverefresh()\">保存间隔</button>"
        "<div class='msg' id='rmsg'></div>"
        "<p class='hint'>设备上也可以调：设置页按 OK 循环切换。</p>"
        "<script>function saverefresh(){"
        "fetch('/ui',{method:'POST',body:"
        "'refresh='+document.getElementById('refresh').value});"
        "document.getElementById('rmsg').textContent='已保存 ✓'}</script></div>");

    // ---- 卡 5：服务器管理（SSH 监控） ----
    off += snprintf(page + off, MAX_RESPONSE - off,
        "<div class='card'><h2>5. 服务器（SSH 监控，最多 4 台）</h2>"
        "<div id='srvlist'></div>"
        "<input id='sip' placeholder='IP 或域名'>"
        "<input id='suser' placeholder='SSH 账号'>"
        "<input id='spass' type='password' placeholder='SSH 密码'>"
        "<button onclick=\"saveSrv()\">添加 / 保存修改</button>"
        "<div class='msg' id='smsg'></div>"
        "<p class='hint'>Linux 服务器，SSH 密码认证。每台服务器一个状态页："
        "CPU / 内存 / 磁盘 / Docker 容器。修改时密码留空表示保持不变。</p>"
        "<script>"
        "var editIdx=-1;"
        "function loadSrv(){"
        " fetch('/srvlist').then(r=>r.json()).then(j=>{"
        "  var n=document.getElementById('srvlist');n.innerHTML='';"
        "  j.forEach(s=>{"
        "   var d=document.createElement('div');d.style.cssText='display:flex;"
        "align-items:center;padding:6px 0;border-bottom:1px solid #22354a';"
        "   var t=document.createElement('span');t.style.flex='1';"
        "   t.textContent=s.ip+' ('+s.user+')';d.appendChild(t);"
        "   var b=document.createElement('button');b.type='button';b.textContent='改';"
        "   b.style.cssText='width:56px;padding:8px 0;margin:0;flex:none;"
        "background:#2a4356;color:#dfe9f3;font-size:14px';"
        "   b.onclick=()=>{editIdx=s.i;"
        "    document.getElementById('sip').value=s.ip;"
        "    document.getElementById('suser').value=s.user;"
        "    document.getElementById('spass').value='';"
        "    document.getElementById('smsg').textContent='修改中：'+s.ip+'，密码留空保持不变';};"
        "   var b2=document.createElement('button');b2.type='button';b2.textContent='删';"
        "   b2.style.cssText='width:56px;padding:8px 0;margin:0 0 0 6px;flex:none;"
        "background:#d96357;color:#fff;font-size:14px';"
        "   b2.onclick=()=>{fetch('/srv/del',{method:'POST',body:'i='+s.i})"
        "    .then(()=>loadSrv())};"
        "   d.appendChild(b);d.appendChild(b2);n.appendChild(d);});"
        "  if(j.length>=4) document.getElementById('smsg').textContent='已达 4 台上限';"
        " });"
        "}"
        "function saveSrv(){"
        " var ip=document.getElementById('sip').value;"
        " var u=document.getElementById('suser').value;"
        " var p=document.getElementById('spass').value;"
        " var m=document.getElementById('smsg');"
        " if(!ip){m.textContent='请填写 IP';return}"
        " var body='ip='+encodeURIComponent(ip)"
        "  +'&user='+encodeURIComponent(u)"
        "  +'&pass='+encodeURIComponent(p);"
        " if(editIdx>=0){"
        "  fetch('/srv/set',{method:'POST',body:'i='+editIdx+'&'+body})"
        "   .then(()=>{editIdx=-1;loadSrv();m.textContent='已保存 ✓'});"
        " } else {"
        "  fetch('/srv/add',{method:'POST',body:body}).then(r=>{"
        "   if(r.ok){loadSrv();m.textContent='已添加 ✓'}"
        "   else{m.textContent='添加失败（可能已满 4 台）'}});"
        " }"
        "}"
        "loadSrv();"
        "</script></div>");

    off += snprintf(page + off, MAX_RESPONSE - off,
        "<p style='text-align:center;color:#6e8ca0;font-size:13px'>"
        "热点：%s · 密码：%s</p></body></html>",
        s_ap_name, APP_AP_PASS_8371);

    (void)off;
    send_response(sock, 200, "text/html; charset=utf-8", page, strlen(page));
    free(page);
}

// ---------------- 请求处理 ----------------

static void handle_scan(int sock)
{
    wifi_scan_config_t cfg = {
        .scan_time.active.min = 0,
        .scan_time.active.max = 120,
    };
    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    if (err != ESP_OK) {
        send_response(sock, 500, "application/json", "[]", 2);
        return;
    }
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > 30) n = 30;
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

static void handle_ark(int sock, const char *body)
{
    char ak[USAGE_ARK_AK_MAX] = { 0 }, sk[USAGE_ARK_SK_MAX] = { 0 },
         host[USAGE_ARK_HOST_MAX] = { 0 };
    form_get(body, "ak", ak, sizeof(ak));
    form_get(body, "sk", sk, sizeof(sk));
    form_get(body, "host", host, sizeof(host));
    usage_store_set_ark(ak, sk, host);
    ESP_LOGI(TAG, "火山密钥已保存 (ak 长度 %u)", (unsigned)strlen(ak));
    static const char ok[] = "OK";
    send_response(sock, 200, "text/plain", ok, sizeof(ok) - 1);
}

static void handle_zcode(int sock, const char *body)
{
    char key[USAGE_ZCODE_KEY_MAX] = { 0 };
    form_get(body, "apikey", key, sizeof(key));
    usage_store_set_zcode_key(key);
    ESP_LOGI(TAG, "智谱 Key 已保存 (长度 %u)", (unsigned)strlen(key));
    static const char ok[] = "OK";
    send_response(sock, 200, "text/plain", ok, sizeof(ok) - 1);
}

static void handle_ui(int sock, const char *body)
{
    char val[16] = { 0 };
    form_get(body, "refresh", val, sizeof(val));
    long s = strtol(val, NULL, 10);
    bool valid = false;
    for (size_t i = 0; i < APP_REFRESH_PRESETS_NUM; i++) {
        if ((long)APP_REFRESH_PRESETS_S[i] == s) { valid = true; break; }
    }
    if (valid) {
        usage_store_set_refresh_s((uint32_t)s);
        ESP_LOGI(TAG, "刷新间隔已保存: %lds", s);
    }
    static const char ok[] = "OK";
    send_response(sock, 200, "text/plain", ok, sizeof(ok) - 1);
}

// ---------------- 服务器管理 ----------------

// GET /srvlist → [{"i":槽位,"ip":"..","user":".."},...]
static void handle_srv_list(int sock)
{
    cJSON *arr = cJSON_CreateArray();
    usage_srv_t srvs[USAGE_SRV_MAX];
    int n = usage_store_srv_list(srvs, USAGE_SRV_MAX);
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "i", srvs[i].idx);
        cJSON_AddStringToObject(item, "ip", srvs[i].ip);
        cJSON_AddStringToObject(item, "user", srvs[i].user);
        cJSON_AddItemToArray(arr, item);
    }
    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!json) {
        send_response(sock, 200, "application/json", "[]", 2);
        return;
    }
    send_response(sock, 200, "application/json", json, strlen(json));
    cJSON_free(json);
}

static void srv_form(const char *body, int *idx,
                     char *ip, size_t ip_len, char *user, size_t user_len,
                     char *pass, size_t pass_len)
{
    char i[4] = { 0 };
    form_get(body, "i", i, sizeof(i));
    form_get(body, "ip", ip, ip_len);
    form_get(body, "user", user, user_len);
    form_get(body, "pass", pass, pass_len);
    *idx = atoi(i);
}

static void handle_srv_add(int sock, const char *body)
{
    (void)sock;
    char ip[USAGE_SRV_IP_MAX] = { 0 }, user[USAGE_SRV_USER_MAX] = { 0 },
         pass[USAGE_SRV_PASS_MAX] = { 0 };
    int idx;
    srv_form(body, &idx, ip, sizeof(ip), user, sizeof(user),
             pass, sizeof(pass));
    if (!ip[0]) {
        send_response(sock, 400, "text/plain", "no ip", 5);
        return;
    }
    int slot = usage_store_srv_add(ip, user, pass);
    ESP_LOGI(TAG, "添加服务器 %s -> 槽位 %d", ip, slot);
    if (slot < 0) {
        send_response(sock, 400, "text/plain", "full", 4);
        return;
    }
    static const char ok[] = "OK";
    send_response(sock, 200, "text/plain", ok, sizeof(ok) - 1);
}

static void handle_srv_set(int sock, const char *body)
{
    (void)sock;
    char ip[USAGE_SRV_IP_MAX] = { 0 }, user[USAGE_SRV_USER_MAX] = { 0 },
         pass[USAGE_SRV_PASS_MAX] = { 0 };
    int idx;
    srv_form(body, &idx, ip, sizeof(ip), user, sizeof(user),
             pass, sizeof(pass));
    if (idx < 0 || idx >= USAGE_SRV_MAX || !ip[0]) {
        send_response(sock, 400, "text/plain", "bad", 3);
        return;
    }
    usage_srv_t cur;
    if (!usage_store_srv_get(idx, &cur)) {
        send_response(sock, 400, "text/plain", "no srv", 6);
        return;
    }
    // 密码留空 = 保持原密码
    usage_store_srv_set(idx, ip, user, pass[0] ? pass : cur.pass);
    ESP_LOGI(TAG, "修改服务器槽位 %d -> %s", idx, ip);
    static const char ok[] = "OK";
    send_response(sock, 200, "text/plain", ok, sizeof(ok) - 1);
}

static void handle_srv_del(int sock, const char *body)
{
    char i[4] = { 0 };
    form_get(body, "i", i, sizeof(i));
    int idx = atoi(i);
    if (idx < 0 || idx >= USAGE_SRV_MAX) {
        send_response(sock, 400, "text/plain", "bad", 3);
        return;
    }
    usage_store_srv_del(idx);
    ESP_LOGI(TAG, "删除服务器槽位 %d", idx);
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

        struct timeval tv = { .tv_sec = HTTP_TIMEOUT_MS / 1000,
                              .tv_usec = (HTTP_TIMEOUT_MS % 1000) * 1000 };
        setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        int n = recv(conn, rx, RX_BUF_SIZE - 1, 0);
        if (n > 0) {
            rx[n] = '\0';
            char *body = strstr(rx, "\r\n\r\n");
            if (body) body += 4;
            bool is_get = strncmp(rx, "GET ", 4) == 0;
            bool is_post = strncmp(rx, "POST ", 5) == 0;

            if (is_post && strncmp(rx, "POST /save", 10) == 0 && body) {
                handle_save(conn, body);
            } else if (is_post && strncmp(rx, "POST /ark", 9) == 0 && body) {
                handle_ark(conn, body);
            } else if (is_post && strncmp(rx, "POST /zcode", 11) == 0 && body) {
                handle_zcode(conn, body);
            } else if (is_post && strncmp(rx, "POST /ui", 8) == 0 && body) {
                handle_ui(conn, body);
            } else if (is_post && strncmp(rx, "POST /srv/add", 13) == 0 && body) {
                handle_srv_add(conn, body);
            } else if (is_post && strncmp(rx, "POST /srv/set", 13) == 0 && body) {
                handle_srv_set(conn, body);
            } else if (is_post && strncmp(rx, "POST /srv/del", 13) == 0 && body) {
                handle_srv_del(conn, body);
            } else if (is_get && strncmp(rx, "GET /srvlist", 12) == 0) {
                handle_srv_list(conn);
            } else if (is_get && strncmp(rx, "GET /scan", 9) == 0) {
                handle_scan(conn);
            } else if (is_get) {
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
    strlcpy(s_ap_name, ap_name ? ap_name : "Usage", sizeof(s_ap_name));
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
    return ESP_OK;
}

bool portal_http_running(void)
{
    return s_running;
}
