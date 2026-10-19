#pragma once

#include <stdbool.h>
#include <stddef.h>

#define PCMON_HOST_MAX 32
#define PCMON_OS_MAX 32
#define PCMON_USER_MAX 24
#define PCMON_CMODEL_MAX 40
#define PCMON_CORES_MAX 8
#define PCMON_LINE_MAX 512

#define PCMON_PROC_MAX 8
#define PCMON_PROC_NAME_MAX 20
#define PCMON_PORT_MAX 8
#define PCMON_PORT_PROC_MAX 20

#define PCMON_GPU_NAME_MAX 28
#define PCMON_GPU_VENDOR_MAX 12

#define PCMON_ARK_PLAN_MAX 12
#define PCMON_ARK_NAME_MAX 16
#define PCMON_ARK_TITLE_MAX 32
#define PCMON_ARK_WINDOWS 3 // 0 = last 5 hours, 1 = week, 2 = month

#define PCMON_TRAE_CMD_MAX 120
#define PCMON_TRAE_ST_MAX 8

#define PCMON_NEED_PROC "proc"
#define PCMON_NEED_PORT "port"
#define PCMON_NEED_GPU "gpu"
#define PCMON_NEED_ARK "ark"

#define PCMON_PROTO_VER 5

typedef struct {
    bool valid;

    bool has_cpu;
    bool has_cores;
    bool has_freq;
    bool has_proc;
    bool has_mem;
    bool has_mem_detail;
    bool has_swap;
    bool has_disk;
    bool has_disk_detail;
    bool has_disk_io;
    bool has_net;
    bool has_net_total;
    bool has_temp;
    bool has_uptime;
    bool has_bat;
    bool has_pc_power;
    bool has_host;
    bool has_os;
    bool has_user;
    bool has_cmodel;

    float cpu;
    int cores;
    float core_pct[PCMON_CORES_MAX];
    float freq_mhz;
    int proc_count;

    float mem;
    float mem_used_gb;
    float mem_total_gb;
    float swap_pct;
    float swap_used_gb;
    float swap_total_gb;

    float disk;
    float disk_used_gb;
    float disk_total_gb;
    float disk_read_mbs;
    float disk_write_mbs;

    float net_down;
    float net_up;
    float net_recv_mb;
    float net_sent_mb;

    float temp;
    long uptime_s;

    float bat;
    bool bat_plugged;
    long bat_time_s;

    char host[PCMON_HOST_MAX];
    char os[PCMON_OS_MAX];
    char user[PCMON_USER_MAX];
    char cmodel[PCMON_CMODEL_MAX];
} pcmon_stats_t;

typedef struct {
    bool valid;
    int ver;
    bool psutil;
} pcmon_agent_t;

typedef struct {
    char name[PCMON_PROC_NAME_MAX];
    float cpu;    // percent of all cores (Task Manager style)
    float mem_mb; // resident memory in megabytes
} pcmon_proc_t;

typedef struct {
    bool valid;
    int sort; // 0 = by CPU, 1 = by memory
    int count;
    pcmon_proc_t items[PCMON_PROC_MAX];
} pcmon_proc_list_t;

typedef struct {
    int port;
    char proc[PCMON_PORT_PROC_MAX];
} pcmon_port_t;

typedef struct {
    bool valid;
    int count;
    int established; // number of non-listening established connections
    pcmon_port_t items[PCMON_PORT_MAX];
} pcmon_port_list_t;

typedef struct {
    bool valid;
    bool present; // a GPU was reported (name or vendor non-empty)
    char vendor[PCMON_GPU_VENDOR_MAX];
    char name[PCMON_GPU_NAME_MAX];
    bool has_util;
    float util; // percent
    bool has_vram;
    float vram_used_mb;
    float vram_total_mb;
    bool has_temp;
    float temp;
    bool has_fan;
    float fan; // percent
    bool has_power;
    float power; // watts
} pcmon_gpu_t;

typedef struct {
    bool valid;
    int err; // 0 = ok, 1 = keys not configured, 2 = fetch failed
    char title[PCMON_ARK_TITLE_MAX]; // page title (UTF-8, may be Chinese)
    char plan[PCMON_ARK_PLAN_MAX]; // plan tag, e.g. "Pro"
    char name[PCMON_ARK_NAME_MAX]; // owner tag (UTF-8, may be Chinese)
    bool has_win[PCMON_ARK_WINDOWS];
    float used[PCMON_ARK_WINDOWS];
    float quota[PCMON_ARK_WINDOWS];
    long reset_s[PCMON_ARK_WINDOWS]; // seconds until the window resets
} pcmon_ark_t;

// TRAE agent status, from $TSTAT frames sent by the bridge.
typedef enum {
    PCMON_TRAE_IDLE = 0,
    PCMON_TRAE_THINK,
    PCMON_TRAE_CODE,
    PCMON_TRAE_BUILD,
    PCMON_TRAE_PASS,
    PCMON_TRAE_FAIL,
} pcmon_trae_state_t;

typedef struct {
    bool valid;
    int st; // pcmon_trae_state_t
    bool has_task;
    long task;
    bool has_rate;
    int rate; // pass rate percent 0-100
    bool has_cm;
    long cm; // coding minutes today
} pcmon_trae_t;

// A pending RunCommand approval, from $TAPRV frames.
typedef struct {
    bool valid;
    long id;
    char cmd[PCMON_TRAE_CMD_MAX]; // UTF-8 command text, sanitized
} pcmon_trae_aprv_t;

void pcmon_stats_reset(pcmon_stats_t *stats);

void pcmon_proc_list_reset(pcmon_proc_list_t *list);

void pcmon_port_list_reset(pcmon_port_list_t *list);

void pcmon_gpu_reset(pcmon_gpu_t *gpu);

void pcmon_ark_reset(pcmon_ark_t *ark);

bool pcmon_parse_status(const char *frame, pcmon_stats_t *out);

bool pcmon_parse_agent(const char *frame, pcmon_agent_t *out);

bool pcmon_parse_proc_list(const char *frame, pcmon_proc_list_t *out);

bool pcmon_parse_port_list(const char *frame, pcmon_port_list_t *out);

bool pcmon_parse_gpu(const char *frame, pcmon_gpu_t *out);

bool pcmon_parse_ark(const char *frame, pcmon_ark_t *out);

void pcmon_trae_reset(pcmon_trae_t *trae);

void pcmon_trae_aprv_reset(pcmon_trae_aprv_t *aprv);

bool pcmon_parse_trae(const char *frame, pcmon_trae_t *out);

bool pcmon_parse_trae_aprv(const char *frame, pcmon_trae_aprv_t *out);

// need is NULL for a plain request, otherwise PCMON_NEED_PROC / PCMON_NEED_PORT /
// PCMON_NEED_GPU / PCMON_NEED_ARK.
int pcmon_format_request(char *out, size_t n, const char *need, int sort);

int pcmon_format_status(const pcmon_stats_t *stats, char *out, size_t n);

int pcmon_format_agent(int psutil, char *out, size_t n);

int pcmon_format_proc_list(const pcmon_proc_list_t *list, char *out, size_t n);

int pcmon_format_port_list(const pcmon_port_list_t *list, char *out, size_t n);

int pcmon_format_gpu(const pcmon_gpu_t *gpu, char *out, size_t n);

int pcmon_format_ark(const pcmon_ark_t *ark, char *out, size_t n);

// Device -> bridge decision for a $TAPRV request: r=allow|deny.
int pcmon_format_trae_btn(long id, bool allow, char *out, size_t n);

int pcmon_level(float percent);
