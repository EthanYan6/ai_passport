#include "pcmon_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PCMON_PREFIX "$PCSTAT,"
#define PCMON_AGENT_PREFIX "$PCAGENT,"
#define PCMON_PROC_PREFIX "$PCPROC,"
#define PCMON_PORT_PREFIX "$PCPORT,"
#define PCMON_GPU_PREFIX "$PCGPU,"

void pcmon_stats_reset(pcmon_stats_t *stats)
{
    if (!stats) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
}

void pcmon_proc_list_reset(pcmon_proc_list_t *list)
{
    if (!list) {
        return;
    }
    memset(list, 0, sizeof(*list));
}

void pcmon_port_list_reset(pcmon_port_list_t *list)
{
    if (!list) {
        return;
    }
    memset(list, 0, sizeof(*list));
}

void pcmon_gpu_reset(pcmon_gpu_t *gpu)
{
    if (!gpu) {
        return;
    }
    memset(gpu, 0, sizeof(*gpu));
}

static void copy_field(char *dst, size_t n, const char *src, size_t len)
{
    if (len >= n) {
        len = n - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static bool key_is(const char *key, size_t klen, const char *name)
{
    return strlen(name) == klen && strncmp(key, name, klen) == 0;
}

static int core_index(const char *key, size_t klen)
{
    if (klen != 2 || key[0] != 'c') {
        return -1;
    }
    if (key[1] < '0' || key[1] > '9') {
        return -1;
    }
    int idx = key[1] - '0';
    return idx < PCMON_CORES_MAX ? idx : -1;
}

// Returns the numeric suffix of a key like "p3" / "u12", or -1 if the key does
// not use the given prefix or is not a plain decimal index.
static int indexed_key(const char *key, size_t klen, char prefix)
{
    if (klen < 2 || key[0] != prefix) {
        return -1;
    }
    int value = 0;
    for (size_t i = 1; i < klen; i++) {
        if (key[i] < '0' || key[i] > '9') {
            return -1;
        }
        value = value * 10 + (key[i] - '0');
        if (value > 99) {
            return -1;
        }
    }
    return value;
}

bool pcmon_parse_status(const char *frame, pcmon_stats_t *out)
{
    if (!frame || !out) {
        return false;
    }
    const char *start = strstr(frame, PCMON_PREFIX);
    if (!start) {
        return false;
    }
    const char *p = start + strlen(PCMON_PREFIX);
    const char *end = strchr(p, '#');
    if (!end) {
        return false;
    }

    pcmon_stats_t st;
    pcmon_stats_reset(&st);

    while (p < end) {
        const char *comma = memchr(p, ',', (size_t)(end - p));
        const char *tok_end = comma ? comma : end;
        const char *eq = memchr(p, '=', (size_t)(tok_end - p));
        if (eq) {
            const char *key = p;
            size_t klen = (size_t)(eq - p);
            const char *val = eq + 1;
            size_t vlen = (size_t)(tok_end - val);
            if (key_is(key, klen, "cpu")) {
                st.cpu = (float)strtod(val, NULL);
                st.has_cpu = true;
            } else if (key_is(key, klen, "ccnt")) {
                st.cores = (int)strtol(val, NULL, 10);
                st.has_cores = true;
            } else if (key_is(key, klen, "freq")) {
                st.freq_mhz = (float)strtod(val, NULL);
                st.has_freq = true;
            } else if (key_is(key, klen, "proc")) {
                st.proc_count = (int)strtol(val, NULL, 10);
                st.has_proc = true;
            } else if (key_is(key, klen, "mem")) {
                st.mem = (float)strtod(val, NULL);
                st.has_mem = true;
            } else if (key_is(key, klen, "mused")) {
                st.mem_used_gb = (float)strtod(val, NULL);
                st.has_mem_detail = true;
            } else if (key_is(key, klen, "mtotal")) {
                st.mem_total_gb = (float)strtod(val, NULL);
                st.has_mem_detail = true;
            } else if (key_is(key, klen, "swap")) {
                st.swap_pct = (float)strtod(val, NULL);
                st.has_swap = true;
            } else if (key_is(key, klen, "sused")) {
                st.swap_used_gb = (float)strtod(val, NULL);
                st.has_swap = true;
            } else if (key_is(key, klen, "stotal")) {
                st.swap_total_gb = (float)strtod(val, NULL);
                st.has_swap = true;
            } else if (key_is(key, klen, "disk")) {
                st.disk = (float)strtod(val, NULL);
                st.has_disk = true;
            } else if (key_is(key, klen, "dused")) {
                st.disk_used_gb = (float)strtod(val, NULL);
                st.has_disk_detail = true;
            } else if (key_is(key, klen, "dtotal")) {
                st.disk_total_gb = (float)strtod(val, NULL);
                st.has_disk_detail = true;
            } else if (key_is(key, klen, "dread")) {
                st.disk_read_mbs = (float)strtod(val, NULL);
                st.has_disk_io = true;
            } else if (key_is(key, klen, "dwrite")) {
                st.disk_write_mbs = (float)strtod(val, NULL);
                st.has_disk_io = true;
            } else if (key_is(key, klen, "netd")) {
                st.net_down = (float)strtod(val, NULL);
                st.has_net = true;
            } else if (key_is(key, klen, "netu")) {
                st.net_up = (float)strtod(val, NULL);
                st.has_net = true;
            } else if (key_is(key, klen, "nrecv")) {
                st.net_recv_mb = (float)strtod(val, NULL);
                st.has_net_total = true;
            } else if (key_is(key, klen, "nsent")) {
                st.net_sent_mb = (float)strtod(val, NULL);
                st.has_net_total = true;
            } else if (key_is(key, klen, "temp")) {
                st.temp = (float)strtod(val, NULL);
                st.has_temp = true;
            } else if (key_is(key, klen, "up")) {
                st.uptime_s = strtol(val, NULL, 10);
                st.has_uptime = true;
            } else if (key_is(key, klen, "bat")) {
                st.bat = (float)strtod(val, NULL);
                st.has_bat = true;
            } else if (key_is(key, klen, "bplug")) {
                st.bat_plugged = strtol(val, NULL, 10) != 0;
                st.has_pc_power = true;
            } else if (key_is(key, klen, "btime")) {
                st.bat_time_s = strtol(val, NULL, 10);
                st.has_pc_power = true;
            } else if (key_is(key, klen, "host")) {
                copy_field(st.host, sizeof(st.host), val, vlen);
                st.has_host = true;
            } else if (key_is(key, klen, "os")) {
                copy_field(st.os, sizeof(st.os), val, vlen);
                st.has_os = true;
            } else if (key_is(key, klen, "user")) {
                copy_field(st.user, sizeof(st.user), val, vlen);
                st.has_user = true;
            } else if (key_is(key, klen, "cmodel")) {
                copy_field(st.cmodel, sizeof(st.cmodel), val, vlen);
                st.has_cmodel = true;
            } else {
                int idx = core_index(key, klen);
                if (idx >= 0) {
                    st.core_pct[idx] = (float)strtod(val, NULL);
                    if (st.cores < idx + 1) {
                        st.cores = idx + 1;
                    }
                    st.has_cores = true;
                }
            }
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }

    st.valid = st.has_cpu && st.has_mem;
    *out = st;
    return st.valid;
}

bool pcmon_parse_agent(const char *frame, pcmon_agent_t *out)
{
    if (!frame || !out) {
        return false;
    }
    const char *start = strstr(frame, PCMON_AGENT_PREFIX);
    if (!start) {
        return false;
    }
    const char *p = start + strlen(PCMON_AGENT_PREFIX);
    const char *end = strchr(p, '#');
    if (!end) {
        return false;
    }

    pcmon_agent_t ag;
    memset(&ag, 0, sizeof(ag));

    while (p < end) {
        const char *comma = memchr(p, ',', (size_t)(end - p));
        const char *tok_end = comma ? comma : end;
        const char *eq = memchr(p, '=', (size_t)(tok_end - p));
        if (eq) {
            const char *key = p;
            size_t klen = (size_t)(eq - p);
            const char *val = eq + 1;
            if (key_is(key, klen, "ver")) {
                ag.ver = (int)strtol(val, NULL, 10);
            } else if (key_is(key, klen, "psutil")) {
                ag.psutil = strtol(val, NULL, 10) != 0;
            }
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }

    ag.valid = true;
    *out = ag;
    return true;
}

bool pcmon_parse_proc_list(const char *frame, pcmon_proc_list_t *out)
{
    if (!frame || !out) {
        return false;
    }
    const char *start = strstr(frame, PCMON_PROC_PREFIX);
    if (!start) {
        return false;
    }
    const char *p = start + strlen(PCMON_PROC_PREFIX);
    const char *end = strchr(p, '#');
    if (!end) {
        return false;
    }

    pcmon_proc_list_t list;
    pcmon_proc_list_reset(&list);

    while (p < end) {
        const char *comma = memchr(p, ',', (size_t)(end - p));
        const char *tok_end = comma ? comma : end;
        const char *eq = memchr(p, '=', (size_t)(tok_end - p));
        if (eq) {
            const char *key = p;
            size_t klen = (size_t)(eq - p);
            const char *val = eq + 1;
            size_t vlen = (size_t)(tok_end - val);
            if (key_is(key, klen, "by")) {
                list.sort = (int)strtol(val, NULL, 10);
            } else if (key_is(key, klen, "cnt")) {
                list.count = (int)strtol(val, NULL, 10);
            } else {
                int idx = -1;
                if ((idx = indexed_key(key, klen, 'p')) >= 0) {
                    if (idx < PCMON_PROC_MAX) {
                        copy_field(list.items[idx].name, sizeof(list.items[idx].name), val, vlen);
                    }
                } else if ((idx = indexed_key(key, klen, 'u')) >= 0) {
                    if (idx < PCMON_PROC_MAX) {
                        list.items[idx].cpu = (float)strtod(val, NULL);
                    }
                } else if ((idx = indexed_key(key, klen, 'm')) >= 0) {
                    if (idx < PCMON_PROC_MAX) {
                        list.items[idx].mem_mb = (float)strtod(val, NULL);
                    }
                }
            }
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }

    if (list.count < 0) {
        list.count = 0;
    }
    if (list.count > PCMON_PROC_MAX) {
        list.count = PCMON_PROC_MAX;
    }
    list.valid = true;
    *out = list;
    return true;
}

bool pcmon_parse_port_list(const char *frame, pcmon_port_list_t *out)
{
    if (!frame || !out) {
        return false;
    }
    const char *start = strstr(frame, PCMON_PORT_PREFIX);
    if (!start) {
        return false;
    }
    const char *p = start + strlen(PCMON_PORT_PREFIX);
    const char *end = strchr(p, '#');
    if (!end) {
        return false;
    }

    pcmon_port_list_t list;
    pcmon_port_list_reset(&list);

    while (p < end) {
        const char *comma = memchr(p, ',', (size_t)(end - p));
        const char *tok_end = comma ? comma : end;
        const char *eq = memchr(p, '=', (size_t)(tok_end - p));
        if (eq) {
            const char *key = p;
            size_t klen = (size_t)(eq - p);
            const char *val = eq + 1;
            size_t vlen = (size_t)(tok_end - val);
            if (key_is(key, klen, "cnt")) {
                list.count = (int)strtol(val, NULL, 10);
            } else if (key_is(key, klen, "est")) {
                list.established = (int)strtol(val, NULL, 10);
            } else {
                int idx = -1;
                if ((idx = indexed_key(key, klen, 't')) >= 0) {
                    if (idx < PCMON_PORT_MAX) {
                        list.items[idx].port = (int)strtol(val, NULL, 10);
                    }
                } else if ((idx = indexed_key(key, klen, 'n')) >= 0) {
                    if (idx < PCMON_PORT_MAX) {
                        copy_field(list.items[idx].proc, sizeof(list.items[idx].proc), val, vlen);
                    }
                }
            }
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }

    if (list.count < 0) {
        list.count = 0;
    }
    if (list.count > PCMON_PORT_MAX) {
        list.count = PCMON_PORT_MAX;
    }
    list.valid = true;
    *out = list;
    return true;
}

bool pcmon_parse_gpu(const char *frame, pcmon_gpu_t *out)
{
    if (!frame || !out) {
        return false;
    }
    const char *start = strstr(frame, PCMON_GPU_PREFIX);
    if (!start) {
        return false;
    }
    const char *p = start + strlen(PCMON_GPU_PREFIX);
    const char *end = strchr(p, '#');
    if (!end) {
        return false;
    }

    pcmon_gpu_t gpu;
    pcmon_gpu_reset(&gpu);

    while (p < end) {
        const char *comma = memchr(p, ',', (size_t)(end - p));
        const char *tok_end = comma ? comma : end;
        const char *eq = memchr(p, '=', (size_t)(tok_end - p));
        if (eq) {
            const char *key = p;
            size_t klen = (size_t)(eq - p);
            const char *val = eq + 1;
            size_t vlen = (size_t)(tok_end - val);
            if (key_is(key, klen, "v")) {
                copy_field(gpu.vendor, sizeof(gpu.vendor), val, vlen);
            } else if (key_is(key, klen, "n")) {
                copy_field(gpu.name, sizeof(gpu.name), val, vlen);
            } else if (key_is(key, klen, "u")) {
                gpu.util = (float)strtod(val, NULL);
                gpu.has_util = true;
            } else if (key_is(key, klen, "mu")) {
                gpu.vram_used_mb = (float)strtod(val, NULL);
                gpu.has_vram = true;
            } else if (key_is(key, klen, "mt")) {
                gpu.vram_total_mb = (float)strtod(val, NULL);
                gpu.has_vram = true;
            } else if (key_is(key, klen, "t")) {
                gpu.temp = (float)strtod(val, NULL);
                gpu.has_temp = true;
            } else if (key_is(key, klen, "fn")) {
                gpu.fan = (float)strtod(val, NULL);
                gpu.has_fan = true;
            } else if (key_is(key, klen, "pw")) {
                gpu.power = (float)strtod(val, NULL);
                gpu.has_power = true;
            }
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }

    gpu.present = gpu.name[0] != '\0' || gpu.vendor[0] != '\0';
    gpu.valid = true;
    *out = gpu;
    return true;
}

int pcmon_format_request(char *out, size_t n, const char *need, int sort)
{
    if (!out || n == 0) {
        return -1;
    }
    if (!need || !need[0]) {
        return snprintf(out, n, "$REQ#\n");
    }
    return snprintf(out, n, "$REQ,need=%s,sort=%d#\n", need, sort);
}

int pcmon_format_status(const pcmon_stats_t *stats, char *out, size_t n)
{
    if (!stats || !out || n == 0) {
        return -1;
    }

    int off = 0;
    int r = snprintf(out, n, "%s", PCMON_PREFIX);
    if (r < 0 || (size_t)r >= n) {
        return -1;
    }
    off = r;

#define PCMON_APP(...)                                                      \
    do {                                                                    \
        r = snprintf(out + off, n - (size_t)off, __VA_ARGS__);              \
        if (r < 0 || (size_t)r >= n - (size_t)off) {                        \
            return -1;                                                      \
        }                                                                   \
        off += r;                                                           \
    } while (0)

    PCMON_APP("cpu=%.1f", (double)stats->cpu);
    if (stats->has_cores) {
        PCMON_APP(",ccnt=%d", stats->cores);
        int shown = stats->cores;
        if (shown > PCMON_CORES_MAX) {
            shown = PCMON_CORES_MAX;
        }
        for (int i = 0; i < shown; i++) {
            PCMON_APP(",c%d=%.1f", i, (double)stats->core_pct[i]);
        }
    }
    if (stats->has_freq) {
        PCMON_APP(",freq=%.1f", (double)stats->freq_mhz);
    }
    if (stats->has_proc) {
        PCMON_APP(",proc=%d", stats->proc_count);
    }
    PCMON_APP(",mem=%.1f", (double)stats->mem);
    if (stats->has_mem_detail) {
        PCMON_APP(",mused=%.2f,mtotal=%.2f", (double)stats->mem_used_gb,
                  (double)stats->mem_total_gb);
    }
    if (stats->has_swap) {
        PCMON_APP(",swap=%.1f,sused=%.2f,stotal=%.2f", (double)stats->swap_pct,
                  (double)stats->swap_used_gb, (double)stats->swap_total_gb);
    }
    PCMON_APP(",disk=%.1f", (double)stats->disk);
    if (stats->has_disk_detail) {
        PCMON_APP(",dused=%.2f,dtotal=%.2f", (double)stats->disk_used_gb,
                  (double)stats->disk_total_gb);
    }
    if (stats->has_disk_io) {
        PCMON_APP(",dread=%.2f,dwrite=%.2f", (double)stats->disk_read_mbs,
                  (double)stats->disk_write_mbs);
    }
    PCMON_APP(",netd=%.3f,netu=%.3f", (double)stats->net_down, (double)stats->net_up);
    if (stats->has_net_total) {
        PCMON_APP(",nrecv=%.1f,nsent=%.1f", (double)stats->net_recv_mb,
                  (double)stats->net_sent_mb);
    }
    if (stats->has_temp) {
        PCMON_APP(",temp=%.1f", (double)stats->temp);
    }
    if (stats->has_uptime) {
        PCMON_APP(",up=%ld", stats->uptime_s);
    }
    if (stats->has_bat) {
        PCMON_APP(",bat=%.1f", (double)stats->bat);
    }
    if (stats->has_pc_power) {
        PCMON_APP(",bplug=%d,btime=%ld", stats->bat_plugged ? 1 : 0, stats->bat_time_s);
    }
    if (stats->has_host) {
        PCMON_APP(",host=%s", stats->host);
    }
    if (stats->has_os) {
        PCMON_APP(",os=%s", stats->os);
    }
    if (stats->has_user) {
        PCMON_APP(",user=%s", stats->user);
    }
    if (stats->has_cmodel) {
        PCMON_APP(",cmodel=%s", stats->cmodel);
    }
    PCMON_APP("#\n");

#undef PCMON_APP

    return off;
}

int pcmon_format_agent(int psutil, char *out, size_t n)
{
    if (!out || n == 0) {
        return -1;
    }
    return snprintf(out, n, "%sver=%d,psutil=%d#\n", PCMON_AGENT_PREFIX,
                    PCMON_PROTO_VER, psutil ? 1 : 0);
}

int pcmon_format_proc_list(const pcmon_proc_list_t *list, char *out, size_t n)
{
    if (!list || !out || n == 0) {
        return -1;
    }
    int count = list->count;
    if (count < 0) {
        count = 0;
    }
    if (count > PCMON_PROC_MAX) {
        count = PCMON_PROC_MAX;
    }

    int off = 0;
    int r = snprintf(out, n, "%sby=%d,cnt=%d", PCMON_PROC_PREFIX, list->sort, count);
    if (r < 0 || (size_t)r >= n) {
        return -1;
    }
    off = r;

    for (int i = 0; i < count; i++) {
        r = snprintf(out + off, n - (size_t)off, ",p%d=%s,u%d=%.1f,m%d=%.0f", i,
                     list->items[i].name, i, (double)list->items[i].cpu, i,
                     (double)list->items[i].mem_mb);
        if (r < 0 || (size_t)r >= n - (size_t)off) {
            return -1;
        }
        off += r;
    }

    r = snprintf(out + off, n - (size_t)off, "#\n");
    if (r < 0 || (size_t)r >= n - (size_t)off) {
        return -1;
    }
    off += r;
    return off;
}

int pcmon_format_port_list(const pcmon_port_list_t *list, char *out, size_t n)
{
    if (!list || !out || n == 0) {
        return -1;
    }
    int count = list->count;
    if (count < 0) {
        count = 0;
    }
    if (count > PCMON_PORT_MAX) {
        count = PCMON_PORT_MAX;
    }

    int off = 0;
    int r = snprintf(out, n, "%scnt=%d,est=%d", PCMON_PORT_PREFIX, count,
                     list->established);
    if (r < 0 || (size_t)r >= n) {
        return -1;
    }
    off = r;

    for (int i = 0; i < count; i++) {
        r = snprintf(out + off, n - (size_t)off, ",t%d=%d,n%d=%s", i,
                     list->items[i].port, i, list->items[i].proc);
        if (r < 0 || (size_t)r >= n - (size_t)off) {
            return -1;
        }
        off += r;
    }

    r = snprintf(out + off, n - (size_t)off, "#\n");
    if (r < 0 || (size_t)r >= n - (size_t)off) {
        return -1;
    }
    off += r;
    return off;
}

int pcmon_format_gpu(const pcmon_gpu_t *gpu, char *out, size_t n)
{
    if (!gpu || !out || n == 0) {
        return -1;
    }

    int off = 0;
    int r = snprintf(out, n, "%sv=%s,n=%s", PCMON_GPU_PREFIX, gpu->vendor,
                     gpu->name);
    if (r < 0 || (size_t)r >= n) {
        return -1;
    }
    off = r;

#define PCMON_GPU_APP(...)                                                  \
    do {                                                                    \
        r = snprintf(out + off, n - (size_t)off, __VA_ARGS__);              \
        if (r < 0 || (size_t)r >= n - (size_t)off) {                        \
            return -1;                                                      \
        }                                                                   \
        off += r;                                                           \
    } while (0)

    if (gpu->has_util) {
        PCMON_GPU_APP(",u=%.1f", (double)gpu->util);
    }
    if (gpu->has_vram) {
        PCMON_GPU_APP(",mu=%.0f,mt=%.0f", (double)gpu->vram_used_mb,
                      (double)gpu->vram_total_mb);
    }
    if (gpu->has_temp) {
        PCMON_GPU_APP(",t=%.1f", (double)gpu->temp);
    }
    if (gpu->has_fan) {
        PCMON_GPU_APP(",fn=%.0f", (double)gpu->fan);
    }
    if (gpu->has_power) {
        PCMON_GPU_APP(",pw=%.1f", (double)gpu->power);
    }
    PCMON_GPU_APP("#\n");

#undef PCMON_GPU_APP

    return off;
}

int pcmon_level(float percent)
{
    if (percent >= 90.0f) {
        return 2;
    }
    if (percent >= 70.0f) {
        return 1;
    }
    return 0;
}
