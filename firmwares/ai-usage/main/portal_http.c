// main/portal_http.c —— 配网门户实现：手写 DNS + HTTP，零依赖 lwIP socket
//
// 设计要点（沿用 minimal-checkin）：
//  - DNS(UDP 53): 所有 A 查询都答 192.168.4.1 → 手机连热点后弹"登录到网络"
//  - HTTP(TCP 80): 单线程顺序 accept。AP 模式最多 2 个连接，简单可靠。
//  - 扫描在 STA 接口进行（APSTA 模式下 esp_wifi_scan_start 可用）。
// 门户卡片：WiFi / 火山方舟 / 智谱 / 阿里云百炼 / 刷新间隔 / 显示设置 / SSH 服务器。
// 凭据分别 POST /save /ark /zcode /bailian 等接口保存进 NVS；显示设置走 /show。
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
#include <strings.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>

static const char *TAG = "portal";

#define PORTAL_PORT      80
#define DNS_PORT         53
#define AP_IP            "192.168.4.1"
#define RX_BUF_SIZE      4096
#define HTTP_TIMEOUT_MS  3000

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

// 从头部块中取 Content-Length（没有该头返回 0）
static size_t header_content_length(const char *hdr)
{
    const char *p = hdr;
    while (p && *p) {
        const char *eol = strstr(p, "\r\n");
        size_t line = eol ? (size_t)(eol - p) : strlen(p);
        if (line >= 15 && strncasecmp(p, "content-length:", 15) == 0) {
            return (size_t)strtoul(p + 15, NULL, 10);
        }
        p = eol ? eol + 2 : NULL;
    }
    return 0;
}

// 收满一个请求：recv 到头部结束（\r\n\r\n），再按 Content-Length 收满 body。
// 浏览器的 fetch 常把请求头与 body 拆成两个 TCP 段发送，单次 recv 只会拿到
// 头部、body 为空，表单字段全丢（此前"保存火山密钥"把 NVS 清空即此因）。
// 返回总字节数（0=断开/超时），*body 指向请求体（可能在超时后仍不完整）。
static int recv_request(int conn, char *rx, size_t cap, char **body)
{
    size_t got = 0;
    char *sep = NULL;
    size_t cl = 0;
    rx[0] = '\0';
    *body = NULL;
    while (got + 1 < cap) {
        int n = recv(conn, rx + got, (int)(cap - 1 - got), 0);
        if (n <= 0) break;
        got += (size_t)n;
        rx[got] = '\0';
        if (!sep) {
            sep = strstr(rx, "\r\n\r\n");
            if (!sep) continue;   // 头部还没收全
            *sep = '\0';          // 截断成独立头部串，请求行不受影响
            cl = header_content_length(rx);
            *body = sep + 4;
            if (cl == 0) break;   // 无 body（GET 等）
        }
        if (got - (size_t)(*body - rx) >= cl) break;   // body 收满
    }
    return (int)got;
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
    "label.chk{display:flex;align-items:center;gap:8px;padding:8px 0;font-size:15px}"
    "label.chk input{width:auto;margin:0;flex:none}"
    "</style></head><body><h1>AI 用量胸牌 · 配网</h1>";

// 无 Content-Length 的流式响应头：响应体由 Connection: close 界定。
// 门户页整页拼装需要 16KB 连续堆缓冲（结果队列/接收缓冲膨胀后拿不到），
// 改为片段边拼边发。
static void send_stream_header(int sock, int code, const char *type)
{
    static char header[160];
    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\nContent-Type: %s\r\n"
             "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
             code, code == 200 ? "OK" : "Error", type);
    send_all(sock, header, strlen(header));
}

static void send_lit(int sock, const char *s)
{
    send_all(sock, s, strlen(s));
}

// 格式化单个片段并直接发送（静态缓冲复用，不占堆）
static void send_frag(int sock, const char *fmt, ...)
{
    static char frag[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(frag, sizeof(frag), fmt, ap);
    va_end(ap);
    send_all(sock, frag, strlen(frag));
}

// 门户页由多个片段拼装，避免巨型字符串常量
static void send_portal_page(int sock)
{
    send_stream_header(sock, 200, "text/html; charset=utf-8");
    send_lit(sock, PORTAL_HTML_HEAD);

    // ---- 卡 1：WiFi ----
    send_lit(sock,
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
        "<script>async function save(){"
        "var s=document.getElementById('ssid').value;"
        "var p=document.getElementById('pass').value;"
        "var m=document.getElementById('msg');"
        "if(!s){m.textContent='请输入 WiFi 名称';return}"
        "m.textContent='正在保存…';"
        "try{var r=await fetch('/save',{method:'POST',body:'ssid='+encodeURIComponent(s)"
        "+'&password='+encodeURIComponent(p)});"
        "if(!r.ok)throw new Error();"
        "m.textContent='已保存，连接结果请查看设备屏幕'"
        "}catch(e){m.textContent='保存失败，请确认仍连接设备热点后重试'}}</script></div>");

    // ---- 卡 2：火山方舟密钥 ----
    char ak[USAGE_ARK_AK_MAX] = { 0 }, sk[USAGE_ARK_SK_MAX] = { 0 },
         host[USAGE_ARK_HOST_MAX] = { 0 }, plan[USAGE_ARK_PLAN_MAX] = { 0 };
    usage_store_get_ark(ak, sizeof(ak), sk, sizeof(sk), host, sizeof(host));
    usage_store_get_ark_plan(plan, sizeof(plan));
    if (!host[0]) host[0] = '\0';   // 留空用固件默认
    send_frag(sock,
        "<div class='card'><h2>2. 火山方舟（Coding Plan 用量）</h2>"
        "<input id='ak' value='%s' placeholder='AccessKey (AK)'>"
        "<input id='sk' type='password' value='%s' placeholder='SecretKey (SK)'>"
        "<input id='host' value='%s' placeholder='Host（可留空）'>"
        "<input id='plan' value='%s' maxlength='31' placeholder='套餐标签（默认 pro）'>"
        "<button onclick=\"saveark()\">保存火山密钥</button>"
        "<div class='msg' id='amsg'></div>"
        "<p class='hint'>火山引擎控制台 → 访问控制 IAM → API 访问密钥。"
        "用量接口与 pc-monitor bridge 相同（V4 签名）。</p>"
        "<script>function saveark(){"
        "fetch('/ark',{method:'POST',body:"
        "'ak='+encodeURIComponent(document.getElementById('ak').value)"
        "+'&sk='+encodeURIComponent(document.getElementById('sk').value)"
        "+'&host='+encodeURIComponent(document.getElementById('host').value)"
        "+'&plan='+encodeURIComponent(document.getElementById('plan').value)});"
        "document.getElementById('amsg').textContent='已保存 ✓'}</script></div>",
        ak, sk, host, plan);

    // ---- 卡 3：智谱 API Key ----
    char zkey[USAGE_ZCODE_KEY_MAX] = { 0 };
    usage_store_get_zcode_key(zkey, sizeof(zkey));
    send_frag(sock,
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

    // ---- 卡 4：阿里云百炼 Coding Plan（AK/SK 自动换取控制台令牌） ----
    send_lit(sock,
        "<div class='card'><h2>4. 阿里云百炼 Coding Plan</h2>"
        "<input id='bak' type='password' placeholder='AccessKey ID' autocomplete=off>"
        "<input id='bsk' type='password' placeholder='AccessKey Secret'>"
        "<button onclick=\"savebailian()\">保存百炼密钥</button>"
        "<div class='msg' id='bmsg'></div>"
        "<p class='hint'>查询 5 小时 / 每周 / 每月真实请求额度。"
        "建议使用最小权限 RAM 子账号，密钥不回显。</p>"
        "<script>function savebailian(){"
        "var m=document.getElementById('bmsg');"
        "var ak=document.getElementById('bak').value;"
        "var sk=document.getElementById('bsk').value;"
        "if(!ak||!sk){m.textContent='请填写 AK 和 SK';return}"
        "fetch('/bailian',{method:'POST',body:"
        "'bak='+encodeURIComponent(ak)+'&bsk='+encodeURIComponent(sk)})"
        ".then(r=>{m.textContent=r.ok?'已保存 ✓':'保存失败'})"
        ".catch(()=>{m.textContent='保存失败，请重试'})}</script></div>");

    // ---- 卡 5：刷新间隔 ----
    send_lit(sock,
        "<div class='card'><h2>5. 刷新间隔</h2><select id='refresh'>");
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
        send_all(sock, opt, strlen(opt));
    }
    send_lit(sock,
        "</select><button onclick=\"saverefresh()\">保存间隔</button>"
        "<div class='msg' id='rmsg'></div>"
        "<p class='hint'>设备上也可以调：设置页按 OK 循环切换。</p>"
        "<script>function saverefresh(){"
        "fetch('/ui',{method:'POST',body:"
        "'refresh='+document.getElementById('refresh').value});"
        "document.getElementById('rmsg').textContent='已保存 ✓'}</script></div>");

    // ---- 卡 6：显示设置（隐藏的平台不进轮播、不再查询） ----
    send_frag(sock,
        "<div class='card'><h2>6. 显示设置（设备轮播页面）</h2>"
        "<label class='chk'><input type='checkbox' id='show_ark'%s> 火山方舟</label>"
        "<label class='chk'><input type='checkbox' id='show_zcode'%s> 智谱 zcode</label>"
        "<label class='chk'><input type='checkbox' id='show_bailian'%s> 阿里云百炼</label>"
        "<button onclick=\"saveshow()\">保存显示</button>"
        "<div class='msg' id='vmsg'></div>"
        "<p class='hint'>取消勾选的平台不出现在设备轮播，也不再查询其用量；"
        "重新勾选保存后下一轮刷新自动恢复。</p>"
        "<script>function saveshow(){"
        "fetch('/show',{method:'POST',body:"
        "'ark='+(document.getElementById('show_ark').checked?1:0)"
        "+'&zcode='+(document.getElementById('show_zcode').checked?1:0)"
        "+'&bailian='+(document.getElementById('show_bailian').checked?1:0)});"
        "document.getElementById('vmsg').textContent='已保存 ✓'}</script></div>",
        usage_store_get_show_ark() ? " checked" : "",
        usage_store_get_show_zcode() ? " checked" : "",
        usage_store_get_show_bailian() ? " checked" : "");

    // ---- 卡 7：服务器管理（SSH 监控） ----
    send_lit(sock,
        "<div class='card'><h2>7. 服务器（SSH 监控，最多 4 台）</h2>"
        "<div id='srvlist'></div>"
        "<input id='sip' placeholder='IP 或域名'>"
        "<input id='sport' type='number' min='1' max='65535' value='22' "
        "placeholder='SSH 端口（默认 22）'>"
        "<input id='suser' placeholder='SSH 账号'>"
        "<input id='spass' type='password' placeholder='SSH 密码'>"
        "<button onclick=\"saveSrv()\">添加 / 保存修改</button>"
        "<div class='msg' id='smsg'></div>"
        "<p class='hint'>Linux 服务器，SSH 密码认证，端口默认 22，可按实际修改。"
        "每台服务器一个状态页：CPU / 内存 / 磁盘 / Docker 容器。"
        "修改时密码留空表示保持不变。</p>"
        "<script>"
        "var editIdx=-1;"
        "function loadSrv(){"
        " fetch('/srvlist').then(r=>r.json()).then(j=>{"
        "  var n=document.getElementById('srvlist');n.innerHTML='';"
        "  j.forEach(s=>{"
        "   var d=document.createElement('div');d.style.cssText='display:flex;"
        "align-items:center;padding:6px 0;border-bottom:1px solid #22354a';"
        "   var t=document.createElement('span');t.style.flex='1';"
        "   t.textContent=s.ip+':'+s.port+' ('+s.user+')';d.appendChild(t);"
        "   var b=document.createElement('button');b.type='button';b.textContent='改';"
        "   b.style.cssText='width:56px;padding:8px 0;margin:0;flex:none;"
        "background:#2a4356;color:#dfe9f3;font-size:14px';"
        "   b.onclick=()=>{editIdx=s.i;"
        "    document.getElementById('sip').value=s.ip;"
        "    document.getElementById('sport').value=s.port||22;"
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
        " var port=document.getElementById('sport').value||'22';"
        " var u=document.getElementById('suser').value;"
        " var p=document.getElementById('spass').value;"
        " var m=document.getElementById('smsg');"
        " if(!ip){m.textContent='请填写 IP';return}"
        " if(port<1||port>65535){m.textContent='端口范围 1-65535';return}"
        " var body='ip='+encodeURIComponent(ip)"
        "  +'&port='+encodeURIComponent(port)"
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

    send_frag(sock,
        "<p style='text-align:center;color:#6e8ca0;font-size:13px'>"
        "热点：%s · 密码：%s</p></body></html>",
        s_ap_name, APP_AP_PASS_8371);
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
    esp_err_t err = wifi_manager_connect(ssid, pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存或发起 WiFi 连接失败: %s", esp_err_to_name(err));
        static const char fail[] = "connect request failed";
        send_response(sock, 500, "text/plain", fail, sizeof(fail) - 1);
        return;
    }
    static const char ok[] = "OK";
    send_response(sock, 200, "text/plain", ok, sizeof(ok) - 1);
}

static void handle_ark(int sock, const char *body)
{
    char ak[USAGE_ARK_AK_MAX] = { 0 }, sk[USAGE_ARK_SK_MAX] = { 0 },
         host[USAGE_ARK_HOST_MAX] = { 0 }, plan[USAGE_ARK_PLAN_MAX] = { 0 };
    // 字段在请求体中缺失（body 不完整）时保留 NVS 原值，防止误清空；
    // 字段存在但为空则是表单本意（host 可留空清掉覆盖值）。
    bool has_ak = form_get(body, "ak", ak, sizeof(ak));
    bool has_sk = form_get(body, "sk", sk, sizeof(sk));
    bool has_host = form_get(body, "host", host, sizeof(host));
    bool has_plan = form_get(body, "plan", plan, sizeof(plan));
    char oak[USAGE_ARK_AK_MAX] = { 0 }, osk[USAGE_ARK_SK_MAX] = { 0 },
         ohost[USAGE_ARK_HOST_MAX] = { 0 }, oplan[USAGE_ARK_PLAN_MAX] = { 0 };
    usage_store_get_ark(oak, sizeof(oak), osk, sizeof(osk),
                        ohost, sizeof(ohost));
    usage_store_get_ark_plan(oplan, sizeof(oplan));
    usage_store_set_ark(has_ak ? ak : oak,
                        has_sk ? sk : osk,
                        has_host ? host : ohost);
    usage_store_set_ark_plan(has_plan ? plan : oplan);
    usage_store_get_ark(oak, sizeof(oak), osk, sizeof(osk),
                        ohost, sizeof(ohost));
    ESP_LOGI(TAG, "火山密钥已保存 (ak len %u sk len %u)",
             (unsigned)strlen(oak), (unsigned)strlen(osk));
    if (!has_ak || !has_sk) {
        ESP_LOGW(TAG, "火山请求体不完整（ak=%d sk=%d），保留原值",
                 has_ak, has_sk);
    }
    static const char ok[] = "OK";
    send_response(sock, 200, "text/plain", ok, sizeof(ok) - 1);
}

static void handle_zcode(int sock, const char *body)
{
    char key[USAGE_ZCODE_KEY_MAX] = { 0 };
    bool has_key = form_get(body, "apikey", key, sizeof(key));
    char okey[USAGE_ZCODE_KEY_MAX] = { 0 };
    usage_store_get_zcode_key(okey, sizeof(okey));
    usage_store_set_zcode_key(has_key ? key : okey);
    usage_store_get_zcode_key(okey, sizeof(okey));
    ESP_LOGI(TAG, "智谱 Key 已保存 (长度 %u)", (unsigned)strlen(okey));
    static const char ok[] = "OK";
    send_response(sock, 200, "text/plain", ok, sizeof(ok) - 1);
}

static void handle_bailian(int sock, const char *body)
{
    char ak[USAGE_BAILIAN_AK_MAX] = { 0 };
    char sk[USAGE_BAILIAN_SK_MAX] = { 0 };
    bool has_ak = form_get(body, "bak", ak, sizeof(ak));
    bool has_sk = form_get(body, "bsk", sk, sizeof(sk));
    char old_ak[USAGE_BAILIAN_AK_MAX] = { 0 };
    char old_sk[USAGE_BAILIAN_SK_MAX] = { 0 };
    usage_store_get_bailian(old_ak, sizeof(old_ak), old_sk, sizeof(old_sk));
    usage_store_set_bailian(has_ak ? ak : old_ak, has_sk ? sk : old_sk);
    ESP_LOGI(TAG, "百炼密钥已保存 (ak=%d sk=%d)", has_ak, has_sk);
    memset(old_sk, 0, sizeof(old_sk));
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

// POST /show：平台页显示开关（ark/zcode/bailian=0|1）。
// 字段缺席 = 保持原值，避免异常请求把平台全部隐藏。
static void handle_show(int sock, const char *body)
{
    char v[8] = { 0 };
    if (form_get(body, "ark", v, sizeof(v)) && (v[0] == '0' || v[0] == '1')) {
        usage_store_set_show_ark(v[0] == '1');
    }
    if (form_get(body, "zcode", v, sizeof(v)) && (v[0] == '0' || v[0] == '1')) {
        usage_store_set_show_zcode(v[0] == '1');
    }
    if (form_get(body, "bailian", v, sizeof(v)) && (v[0] == '0' || v[0] == '1')) {
        usage_store_set_show_bailian(v[0] == '1');
    }
    ESP_LOGI(TAG, "显示设置已保存: ark=%d zcode=%d bailian=%d",
             usage_store_get_show_ark(), usage_store_get_show_zcode(),
             usage_store_get_show_bailian());
    static const char ok[] = "OK";
    send_response(sock, 200, "text/plain", ok, sizeof(ok) - 1);
}

// ---------------- 服务器管理 ----------------

// GET /srvlist → [{"i":槽位,"ip":"..","port":22,"user":".."},...]
static void handle_srv_list(int sock)
{
    cJSON *arr = cJSON_CreateArray();
    usage_srv_t srvs[USAGE_SRV_MAX];
    int n = usage_store_srv_list(srvs, USAGE_SRV_MAX);
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "i", srvs[i].idx);
        cJSON_AddStringToObject(item, "ip", srvs[i].ip);
        cJSON_AddNumberToObject(item, "port", srvs[i].port);
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

static bool srv_form(const char *body, int *idx,
                     char *ip, size_t ip_len, char *user, size_t user_len,
                     char *pass, size_t pass_len, uint16_t *port)
{
    char i[4] = { 0 };
    form_get(body, "i", i, sizeof(i));
    form_get(body, "ip", ip, ip_len);
    form_get(body, "user", user, user_len);
    form_get(body, "pass", pass, pass_len);
    *idx = atoi(i);

    char value[8] = { 0 };
    if (!form_get(body, "port", value, sizeof(value)) || !value[0]) {
        *port = APP_SRV_SSH_PORT;
        return true;
    }
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (!end || *end != '\0' || parsed < 1 || parsed > 65535) return false;
    *port = (uint16_t)parsed;
    return true;
}

static void handle_srv_add(int sock, const char *body)
{
    (void)sock;
    char ip[USAGE_SRV_IP_MAX] = { 0 }, user[USAGE_SRV_USER_MAX] = { 0 },
         pass[USAGE_SRV_PASS_MAX] = { 0 };
    int idx;
    uint16_t port = APP_SRV_SSH_PORT;
    if (!srv_form(body, &idx, ip, sizeof(ip), user, sizeof(user),
                  pass, sizeof(pass), &port) || !ip[0]) {
        send_response(sock, 400, "text/plain", "bad server", 10);
        return;
    }
    int slot = usage_store_srv_add(ip, user, pass, port);
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
    uint16_t port = APP_SRV_SSH_PORT;
    if (!srv_form(body, &idx, ip, sizeof(ip), user, sizeof(user),
                  pass, sizeof(pass), &port) ||
        idx < 0 || idx >= USAGE_SRV_MAX || !ip[0]) {
        send_response(sock, 400, "text/plain", "bad", 3);
        return;
    }
    usage_srv_t cur;
    if (!usage_store_srv_get(idx, &cur)) {
        send_response(sock, 400, "text/plain", "no srv", 6);
        return;
    }
    // 密码留空 = 保持原密码
    usage_store_srv_set(idx, ip, user, pass[0] ? pass : cur.pass, port);
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
    char *body;
    while (s_running && rx) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int conn = accept(sock, (struct sockaddr *)&cli, &cli_len);
        if (conn < 0) continue;

        struct timeval tv = { .tv_sec = HTTP_TIMEOUT_MS / 1000,
                              .tv_usec = (HTTP_TIMEOUT_MS % 1000) * 1000 };
        setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        int n = recv_request(conn, rx, RX_BUF_SIZE, &body);
        if (n > 0 && body) {
            // 记录请求行，串口对账用（手机请求有没有进来、进来的是什么）
            char reqline[48];
            const char *cr = strchr(rx, '\r');
            size_t rl = cr ? (size_t)(cr - rx) : strlen(rx);
            if (rl >= sizeof(reqline)) rl = sizeof(reqline) - 1;
            memcpy(reqline, rx, rl);
            reqline[rl] = '\0';
            ESP_LOGI(TAG, "HTTP %s", reqline);

            bool is_get = strncmp(rx, "GET ", 4) == 0;
            bool is_post = strncmp(rx, "POST ", 5) == 0;

            if (is_post && strncmp(rx, "POST /save", 10) == 0 && body) {
                handle_save(conn, body);
            } else if (is_post && strncmp(rx, "POST /ark", 9) == 0 && body) {
                handle_ark(conn, body);
            } else if (is_post && strncmp(rx, "POST /zcode", 11) == 0 && body) {
                handle_zcode(conn, body);
            } else if (is_post && strncmp(rx, "POST /bailian", 13) == 0 && body) {
                handle_bailian(conn, body);
            } else if (is_post && strncmp(rx, "POST /ui", 8) == 0 && body) {
                handle_ui(conn, body);
            } else if (is_post && strncmp(rx, "POST /show", 10) == 0 && body) {
                handle_show(conn, body);
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
