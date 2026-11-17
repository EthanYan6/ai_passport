// main/srv_client.c —— 服务器状态采集实现（wolfSSH 客户端，密码认证 + exec）
//
// 流程：非阻塞 TCP 连接（select 控超时）→ wolfSSH_connect 由外层 select 驱动
// （IO 回调把 EAGAIN 映射成 WANT_READ/WANT_WRITE，阻塞态状态机可恢复）→
// exec 一条采集命令 → 累积输出到 EOF → 行解析。
// 主机密钥校验：不设 publicKeyCheck 回调 = 接受任意 host key（局域网自用
// 服务器监控的务实取舍，见 wolfSSH internal.c DoKexDhReply：无回调即接受）。
#include "srv_client.h"

#include <wolfssh/ssh.h>
#include <wolfssh/error.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>

static const char *TAG = "srv_client";

#define DEADLINE_MS (int64_t)(APP_SRV_TOTAL_TIMEOUT_MS)

// ---------------- IO 回调 ----------------
// socket 全程非阻塞；EAGAIN/EWOULDBLOCK 映射成 wolfSSH 的 WANT 码，
// 由调用方 select 后重入（wolfSSH 的状态机按 WS_WANT_READ/WRITE 恢复）。

static int io_recv(WOLFSSH *ssh, void *buf, word32 sz, void *ctx)
{
    (void)ssh;
    int fd = *(int *)ctx;
    int r = (int)recv(fd, buf, sz, 0);
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return WS_CBIO_ERR_WANT_READ;
        if (errno == EINTR) return WS_CBIO_ERR_ISR;
        if (errno == ECONNRESET) return WS_CBIO_ERR_CONN_RST;
        return WS_CBIO_ERR_GENERAL;
    }
    if (r == 0) return WS_CBIO_ERR_CONN_CLOSE;
    return r;
}

static int io_send(WOLFSSH *ssh, void *buf, word32 sz, void *ctx)
{
    (void)ssh;
    int fd = *(int *)ctx;
    int w = (int)send(fd, buf, sz, 0);
    if (w < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS)
            return WS_CBIO_ERR_WANT_WRITE;
        if (errno == EINTR) return WS_CBIO_ERR_ISR;
        if (errno == EPIPE || errno == ECONNRESET) return WS_CBIO_ERR_CONN_CLOSE;
        return WS_CBIO_ERR_GENERAL;
    }
    return w;
}

// 密码认证回调：ctx 指向密码字符串（wolfSSH_SetUserAuthCtx 注入）
static int user_auth_cb(byte authType, WS_UserAuthData *data, void *ctx)
{
    if (authType == WOLFSSH_USERAUTH_PASSWORD && ctx != NULL) {
        const char *pass = (const char *)ctx;
        data->sf.password.password = (const byte *)pass;
        data->sf.password.passwordSz = (word32)strlen(pass);
        return WOLFSSH_USERAUTH_SUCCESS;
    }
    return WOLFSSH_USERAUTH_FAILURE;
}

// ---------------- 连接工具 ----------------

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

// 等 fd 可读/可写，直到 deadline；就绪返回 true
static bool wait_io(int fd, bool write, int64_t deadline)
{
    int64_t remain = deadline - now_ms();
    if (remain <= 0) return false;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv = {
        .tv_sec = (long)(remain / 1000),
        .tv_usec = (long)((remain % 1000) * 1000),
    };
    int r = select(fd + 1, write ? NULL : &fds, write ? &fds : NULL, NULL, &tv);
    return r > 0;
}

// 非阻塞 TCP 连接（含 DNS 解析），超时返回 -1
static int tcp_connect(const char *host, uint16_t port, int64_t deadline)
{
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        return -1;
    }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return -1;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    bool ok = true;
    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0 &&
        (errno == EINPROGRESS || errno == EWOULDBLOCK)) {
        ok = wait_io(fd, true, deadline);
        if (ok) {
            int soerr = 0;
            socklen_t len = sizeof(soerr);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len);
            ok = soerr == 0;
        }
    } else if (errno != 0 && errno != EISCONN) {
        ok = false;
    }
    freeaddrinfo(res);
    if (!ok) {
        close(fd);
        return -1;
    }
    return fd;
}

// ---------------- 输出解析 ----------------

// 端口串压缩："0.0.0.0:8000->8000/tcp, :::443->443/tcp" → "8000->8000,443->443"
static void compact_ports(const char *raw, char *out, size_t outlen)
{
    char tmp[512];
    strlcpy(tmp, raw ? raw : "", sizeof(tmp));
    size_t o = 0;
    out[0] = '\0';
    char *save = NULL;
    for (char *tok = strtok_r(tmp, ",", &save);
         tok != NULL && o + 1 < outlen;
         tok = strtok_r(NULL, ",", &save)) {
        while (*tok == ' ') tok++;
        char *arrow = strstr(tok, "->");
        char *slash = strchr(tok, '/');
        int n = 0;
        if (arrow) {
            *arrow = '\0';
            const char *right = arrow + 2;
            char left[16];
            const char *colon = strrchr(tok, ':');
            strlcpy(left, colon ? colon + 1 : tok, sizeof(left));
            char rport[16];
            strlcpy(rport, right, sizeof(rport));
            char *rs = strchr(rport, '/');
            if (rs) *rs = '\0';
            n = snprintf(out + o, outlen - o, o ? ",%s->%s" : "%s->%s",
                         left, rport);
        } else if (tok[0]) {
            // 仅暴露未映射的端口："8000/tcp" → "8000"
            if (slash) *slash = '\0';
            n = snprintf(out + o, outlen - o, o ? ",%s" : "%s", tok);
        }
        if (n < 0) break;
        if ((size_t)n >= outlen - o) break;
        o += (size_t)n;
    }
}

// 逐行解析采集输出。S1/S2 两次 /proc/stat 采样在设备端算 CPU 差值。
static bool parse_output(const char *text, srv_data_t *out)
{
    long s1[3] = { -1, -1, -1 }, s2[3] = { -1, -1, -1 };
    int mem_pct = -1, mem_u = -1, mem_t = -1;
    int dsk_pct = -1, dsk_u = -1, dsk_t = -1;
    int count = 0;
    bool have_cpu = false, have_mem = false, have_dsk = false;

    char line[512];
    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        // 去 \r（命令输出可能带）
        size_t ll = strlen(line);
        if (ll && line[ll - 1] == '\r') line[ll - 1] = '\0';
        p = nl ? nl + 1 : p + len;

        long a, b, c;
        if (sscanf(line, "S1 %ld %ld %ld", &a, &b, &c) == 3) {
            s1[0] = a; s1[1] = b; s1[2] = c;
        } else if (sscanf(line, "S2 %ld %ld %ld", &a, &b, &c) == 3) {
            s2[0] = a; s2[1] = b; s2[2] = c;
        } else if (sscanf(line, "MEM %d %d %d", &mem_pct, &mem_u, &mem_t) == 3) {
            have_mem = mem_t > 0;
        } else if (sscanf(line, "DSK %d %d %d", &dsk_pct, &dsk_u, &dsk_t) == 3) {
            have_dsk = dsk_t > 0;
        } else if (strncmp(line, "C|", 2) == 0) {
            count++;
            if (count <= APP_SRV_CONTAINERS_MAX) {
                // C|id|name|state|ports
                char *fields[5] = { 0 };
                int f = 0;
                char *tok = line + 2;
                for (int i = 0; i < 5 && tok; i++) {
                    char *bar = strchr(tok, '|');
                    if (bar) *bar = '\0';
                    fields[i] = tok;
                    tok = bar ? bar + 1 : NULL;
                }
                srv_container_t *ct = &out->containers[count - 1];
                memset(ct, 0, sizeof(*ct));
                if (fields[0]) strlcpy(ct->id, fields[0], sizeof(ct->id));
                if (fields[1]) strlcpy(ct->name, fields[1], sizeof(ct->name));
                ct->state = (fields[2] && strcmp(fields[2], "running") == 0) ? 0 : 1;
                if (fields[3]) compact_ports(fields[3], ct->ports, sizeof(ct->ports));
            }
        } else if (strcmp(line, "NODOCKER") == 0) {
            count = 0;
        }
        if (p - text >= APP_SRV_RESP_MAX) break;
    }

    if (s1[2] > 0 && s2[2] > 0) {
        long du = s2[0] - s1[0];   // 非空闲 jiffies 增量
        long dt = s2[2] - s1[2];
        if (dt > 0) {
            out->cpu_pct = (float)du * 100.0f / (float)dt;
            have_cpu = true;
        }
    }
    if (have_mem) {
        out->mem_pct = mem_pct;
        out->mem_used_mb = (mem_u + 512) / 1024;
        out->mem_total_mb = (mem_t + 512) / 1024;
    }
    if (have_dsk) {
        out->disk_pct = dsk_pct;
        out->disk_used_gb = (dsk_u / 1024 + 512) / 1024;
        out->disk_total_gb = (dsk_t / 1024 + 512) / 1024;
    }
    out->container_count = count;
    for (int i = 0; i < APP_SRV_CONTAINERS_MAX; i++) {
        out->containers[i].state = count > i ? out->containers[i].state : 1;
    }
    return have_cpu && have_mem && have_dsk;
}

// ---------------- 主流程 ----------------

void srv_client_fetch(const usage_srv_t *srv, srv_data_t *result)
{
    memset(result, 0, sizeof(*result));
    for (int i = 0; i < APP_SRV_CONTAINERS_MAX; i++) {
        result->containers[i].state = 1;
    }
    strlcpy(result->ip, srv->ip, sizeof(result->ip));
    result->port = srv->port;
    strlcpy(result->user, srv->user, sizeof(result->user));
    result->slot = (uint8_t)srv->idx;

    int64_t deadline = now_ms() + DEADLINE_MS;

    int fd = tcp_connect(srv->ip, srv->port, deadline);
    if (fd < 0) {
        result->err = 2;
        return;
    }

    WOLFSSH_CTX *ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_CLIENT, NULL);
    WOLFSSH *ssh = NULL;
    char *acc = malloc(APP_SRV_RESP_MAX + 1);
    bool ok = false;
    if (ctx && acc) {
        wolfSSH_SetIORecv(ctx, io_recv);
        wolfSSH_SetIOSend(ctx, io_send);
        wolfSSH_SetUserAuth(ctx, user_auth_cb);

        ssh = wolfSSH_new(ctx);
        if (ssh) {
            wolfSSH_SetIOReadCtx(ssh, &fd);
            wolfSSH_SetIOWriteCtx(ssh, &fd);
            wolfSSH_SetUsername(ssh, srv->user);
            wolfSSH_SetUserAuthCtx(ssh, srv->pass);
            // exec 命令在 connect 的通道请求阶段带上，必须先设置
            wolfSSH_SetChannelType(ssh, WOLFSSH_SESSION_EXEC,
                                   (byte *)APP_SRV_CMD,
                                   (word32)strlen(APP_SRV_CMD));
        }
    }

    do {
        if (!ssh) break;

        // 握手 + 认证 + 通道建立（外层 select 驱动 WANT_READ/WRITE）
        int ret;
        int err = WS_FATAL_ERROR;
        while ((ret = wolfSSH_connect(ssh)) != WS_SUCCESS) {
            err = wolfSSH_get_error(ssh);
            if (err == WS_WANT_READ) {
                if (!wait_io(fd, false, deadline)) break;
            } else if (err == WS_WANT_WRITE) {
                if (!wait_io(fd, true, deadline)) break;
            } else {
                break;   // 真错误（含认证失败）
            }
        }
        if (ret != WS_SUCCESS) {
            ESP_LOGW(TAG, "%s 握手失败: %s", srv->ip, wolfSSH_get_error_name(ssh));
            result->err = err == WS_USER_AUTH_E ? 3 : 2;
            break;
        }

        // 累积 exec 输出到 EOF/超时
        int total = 0;
        for (;;) {
            if (now_ms() >= deadline || total >= APP_SRV_RESP_MAX) break;
            int r = wolfSSH_stream_read(ssh, (byte *)acc + total,
                                        APP_SRV_RESP_MAX - total);
            if (r > 0) {
                total += r;
                continue;
            }
            int e = wolfSSH_get_error(ssh);
            if (e == WS_WANT_READ) {
                if (!wait_io(fd, false, deadline)) break;
                continue;
            }
            if (e == WS_WANT_WRITE) {
                if (!wait_io(fd, true, deadline)) break;
                continue;
            }
            break;   // WS_EOF / 通道关闭 / 真错误
        }
        acc[total] = '\0';
        ESP_LOGI(TAG, "%s 采集 %d 字节", srv->ip, total);

        ok = parse_output(acc, result);
        if (ok) {
            result->err = 0;
        } else {
            result->err = 4;   // 连上了但数据不完整（非 Linux / 命令失败）
        }
    } while (0);

    free(acc);
    if (ssh) wolfSSH_free(ssh);
    if (ctx) wolfSSH_CTX_free(ctx);
    close(fd);

    if (result->err == 0) {
        result->valid = true;
    }
}
