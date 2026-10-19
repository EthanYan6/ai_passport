#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pcmon_protocol.h"

int main(void)
{
    pcmon_stats_t s;
    pcmon_stats_reset(&s);
    assert(!s.valid);

    const char *frame =
        "$PCSTAT,cpu=12.5,ccnt=4,c0=10.0,c1=20.0,c2=30.0,c3=40.0,freq=3200.0,proc=210,"
        "mem=45.0,mused=7.20,mtotal=16.00,swap=5.0,sused=0.50,stotal=8.00,"
        "disk=60.2,dused=120.50,dtotal=476.00,dread=1.25,dwrite=0.75,"
        "netd=1.500,netu=0.250,nrecv=1024.0,nsent=512.0,"
        "temp=52.0,up=12345,bat=88.0,bplug=1,btime=3600,"
        "host=DESKTOP,os=Windows-11,user=alice,cmodel=AMD Ryzen 5#\n";
    assert(pcmon_parse_status(frame, &s));
    assert(s.valid);
    assert(s.has_cpu && s.cpu > 12.4f && s.cpu < 12.6f);
    assert(s.has_cores && s.cores == 4);
    assert(s.core_pct[0] > 9.9f && s.core_pct[3] > 39.9f && s.core_pct[3] < 40.1f);
    assert(s.has_freq && s.freq_mhz > 3199.0f && s.freq_mhz < 3201.0f);
    assert(s.has_proc && s.proc_count == 210);
    assert(s.has_mem && s.mem > 44.9f && s.mem < 45.1f);
    assert(s.has_mem_detail && s.mem_used_gb > 7.1f && s.mem_total_gb > 15.9f);
    assert(s.has_swap && s.swap_pct > 4.9f && s.swap_total_gb > 7.9f);
    assert(s.has_disk && s.disk > 60.1f && s.disk < 60.3f);
    assert(s.has_disk_detail && s.disk_total_gb > 475.0f);
    assert(s.has_disk_io && s.disk_read_mbs > 1.24f && s.disk_write_mbs > 0.74f);
    assert(s.has_net);
    assert(s.has_net_total && s.net_recv_mb > 1023.0f && s.net_sent_mb > 511.0f);
    assert(s.has_temp);
    assert(s.uptime_s == 12345);
    assert(s.has_bat && s.bat > 87.9f && s.bat < 88.1f);
    assert(s.has_pc_power && s.bat_plugged && s.bat_time_s == 3600);
    assert(strcmp(s.host, "DESKTOP") == 0);
    assert(strcmp(s.os, "Windows-11") == 0);
    assert(strcmp(s.user, "alice") == 0);
    assert(strcmp(s.cmodel, "AMD Ryzen 5") == 0);

    pcmon_stats_t p;
    assert(!pcmon_parse_status("plain text, no frame", &p));
    assert(!pcmon_parse_status("$PCSTAT,cpu=1.0#", &p));
    assert(pcmon_parse_status("log line $PCSTAT,cpu=9,mem=9,xyz=7# trailing", &p));
    assert(p.valid && !p.has_disk && !p.has_host && !p.has_cores);

    const char *core_only = "$PCSTAT,cpu=5.0,mem=50.0,c5=77.0#";
    assert(pcmon_parse_status(core_only, &p));
    assert(p.has_cores && p.cores == 6);
    assert(p.core_pct[5] > 76.9f && p.core_pct[5] < 77.1f);
    assert(p.core_pct[0] < 0.1f);

    pcmon_agent_t ag;
    assert(!pcmon_parse_agent("$PCSTAT,cpu=1,mem=1#", &ag));
    assert(pcmon_parse_agent("noise $PCAGENT,ver=2,psutil=1# tail", &ag));
    assert(ag.valid && ag.ver == 2 && ag.psutil);
    assert(pcmon_parse_agent("$PCAGENT,ver=2,psutil=0#", &ag));
    assert(ag.valid && !ag.psutil);

    char req[40];
    assert(pcmon_format_request(req, sizeof(req), NULL, 0) > 0);
    assert(strncmp(req, "$REQ#", 5) == 0);

    assert(pcmon_format_request(req, sizeof(req), PCMON_NEED_PROC, 1) > 0);
    assert(strcmp(req, "$REQ,need=proc,sort=1#\n") == 0);
    assert(pcmon_format_request(req, sizeof(req), PCMON_NEED_PORT, 0) > 0);
    assert(strcmp(req, "$REQ,need=port,sort=0#\n") == 0);
    assert(pcmon_format_request(req, sizeof(req), PCMON_NEED_GPU, 0) > 0);
    assert(strcmp(req, "$REQ,need=gpu,sort=0#\n") == 0);
    assert(pcmon_format_request(req, sizeof(req), PCMON_NEED_ARK, 0) > 0);
    assert(strcmp(req, "$REQ,need=ark,sort=0#\n") == 0);

    char af[64];
    assert(pcmon_format_agent(1, af, sizeof(af)) > 0);
    assert(pcmon_parse_agent(af, &ag));
    assert(ag.ver == PCMON_PROTO_VER && ag.psutil);

    pcmon_stats_t a;
    pcmon_stats_reset(&a);
    a.valid = true;
    a.has_cpu = a.has_mem = a.has_disk = a.has_net = true;
    a.has_temp = a.has_uptime = a.has_bat = a.has_host = a.has_os = true;
    a.has_cores = a.has_freq = a.has_proc = true;
    a.has_mem_detail = a.has_swap = a.has_disk_detail = a.has_disk_io = true;
    a.has_net_total = a.has_pc_power = a.has_user = a.has_cmodel = true;
    a.cpu = 50.0f;
    a.cores = 3;
    a.core_pct[0] = 11.0f;
    a.core_pct[1] = 22.0f;
    a.core_pct[2] = 33.0f;
    a.freq_mhz = 2400.0f;
    a.proc_count = 180;
    a.mem = 60.0f;
    a.mem_used_gb = 9.5f;
    a.mem_total_gb = 16.0f;
    a.swap_pct = 3.0f;
    a.swap_used_gb = 0.25f;
    a.swap_total_gb = 8.0f;
    a.disk = 70.0f;
    a.disk_used_gb = 333.0f;
    a.disk_total_gb = 476.0f;
    a.disk_read_mbs = 2.5f;
    a.disk_write_mbs = 1.5f;
    a.net_down = 2048.0f;
    a.net_up = 512.0f;
    a.net_recv_mb = 4096.0f;
    a.net_sent_mb = 2048.0f;
    a.temp = 61.0f;
    a.uptime_s = 99;
    a.bat = 77.0f;
    a.bat_plugged = true;
    a.bat_time_s = 1200;
    snprintf(a.host, sizeof(a.host), "BOX");
    snprintf(a.os, sizeof(a.os), "linux");
    snprintf(a.user, sizeof(a.user), "bob");
    snprintf(a.cmodel, sizeof(a.cmodel), "Intel i7");

    char buf[PCMON_LINE_MAX];
    assert(pcmon_format_status(&a, buf, sizeof(buf)) > 0);
    pcmon_stats_t b;
    assert(pcmon_parse_status(buf, &b));
    assert(b.valid && b.uptime_s == 99);
    assert(b.has_host && strcmp(b.host, "BOX") == 0);
    assert(b.has_os && strcmp(b.os, "linux") == 0);
    assert(b.has_user && strcmp(b.user, "bob") == 0);
    assert(b.has_cmodel && strcmp(b.cmodel, "Intel i7") == 0);
    assert(b.cpu > 49.9f && b.cpu < 50.1f);
    assert(b.has_cores && b.cores == 3);
    assert(b.core_pct[1] > 21.9f && b.core_pct[2] > 32.9f);
    assert(b.has_freq && b.freq_mhz > 2399.0f);
    assert(b.has_proc && b.proc_count == 180);
    assert(b.has_mem_detail && b.mem_total_gb > 15.9f);
    assert(b.has_swap && b.swap_total_gb > 7.9f);
    assert(b.has_disk_detail && b.disk_total_gb > 475.0f);
    assert(b.has_disk_io && b.disk_read_mbs > 2.4f);
    assert(b.has_net_total && b.net_recv_mb > 4095.0f);
    assert(b.has_pc_power && b.bat_plugged && b.bat_time_s == 1200);

    const char *proc_frame =
        "$PCPROC,by=0,cnt=2,p0=chrome.exe,u0=12.5,m0=842,p1=Code.exe,u1=3.2,m1=512#\n";
    pcmon_proc_list_t pl;
    assert(pcmon_parse_proc_list(proc_frame, &pl));
    assert(pl.valid && pl.sort == 0 && pl.count == 2);
    assert(strcmp(pl.items[0].name, "chrome.exe") == 0);
    assert(pl.items[0].cpu > 12.4f && pl.items[0].cpu < 12.6f);
    assert(pl.items[0].mem_mb > 841.5f && pl.items[0].mem_mb < 842.5f);
    assert(strcmp(pl.items[1].name, "Code.exe") == 0);
    assert(pl.items[1].cpu > 3.1f && pl.items[1].cpu < 3.3f);

    // A proc frame must not be mistaken for a stats frame, and vice versa.
    pcmon_stats_t not_stats;
    assert(!pcmon_parse_status(proc_frame, &not_stats));

    pcmon_proc_list_t pout;
    pcmon_proc_list_reset(&pout);
    pout.valid = true;
    pout.sort = 1;
    pout.count = 2;
    snprintf(pout.items[0].name, sizeof(pout.items[0].name), "python.exe");
    pout.items[0].cpu = 4.0f;
    pout.items[0].mem_mb = 128.0f;
    snprintf(pout.items[1].name, sizeof(pout.items[1].name), "explorer.exe");
    pout.items[1].cpu = 0.5f;
    pout.items[1].mem_mb = 64.0f;
    char pbuf[PCMON_LINE_MAX];
    assert(pcmon_format_proc_list(&pout, pbuf, sizeof(pbuf)) > 0);
    pcmon_proc_list_t prin;
    assert(pcmon_parse_proc_list(pbuf, &prin));
    assert(prin.sort == 1 && prin.count == 2);
    assert(strcmp(prin.items[0].name, "python.exe") == 0);
    assert(prin.items[0].mem_mb > 127.5f && prin.items[0].mem_mb < 128.5f);
    assert(strcmp(prin.items[1].name, "explorer.exe") == 0);

    const char *port_frame =
        "$PCPORT,cnt=2,est=140,t0=8080,n0=nginx.exe,t1=135,n1=svchost.exe#\n";
    pcmon_port_list_t po;
    assert(pcmon_parse_port_list(port_frame, &po));
    assert(po.valid && po.count == 2 && po.established == 140);
    assert(po.items[0].port == 8080 && strcmp(po.items[0].proc, "nginx.exe") == 0);
    assert(po.items[1].port == 135 && strcmp(po.items[1].proc, "svchost.exe") == 0);
    assert(!pcmon_parse_status(port_frame, &not_stats));

    pcmon_port_list_t poout;
    pcmon_port_list_reset(&poout);
    poout.valid = true;
    poout.count = 1;
    poout.established = 12;
    poout.items[0].port = 3000;
    snprintf(poout.items[0].proc, sizeof(poout.items[0].proc), "node.exe");
    char pobuf[PCMON_LINE_MAX];
    assert(pcmon_format_port_list(&poout, pobuf, sizeof(pobuf)) > 0);
    pcmon_port_list_t poin;
    assert(pcmon_parse_port_list(pobuf, &poin));
    assert(poin.count == 1 && poin.established == 12);
    assert(poin.items[0].port == 3000 && strcmp(poin.items[0].proc, "node.exe") == 0);

    const char *gpu_frame =
        "$PCGPU,v=nvidia,n=NVIDIA GeForce RTX 3050,u=22.0,mu=1179,mt=6144,"
        "t=33.0,fn=30,pw=10.5#\n";
    pcmon_gpu_t g;
    assert(pcmon_parse_gpu(gpu_frame, &g));
    assert(g.valid && g.present);
    assert(strcmp(g.vendor, "nvidia") == 0);
    assert(strcmp(g.name, "NVIDIA GeForce RTX 3050") == 0);
    assert(g.has_util && g.util > 21.9f && g.util < 22.1f);
    assert(g.has_vram && g.vram_used_mb > 1178.5f && g.vram_total_mb > 6143.5f);
    assert(g.has_temp && g.temp > 32.9f && g.temp < 33.1f);
    assert(g.has_fan && g.fan > 29.9f && g.fan < 30.1f);
    assert(g.has_power && g.power > 10.4f && g.power < 10.6f);

    // A GPU frame must not be mistaken for the other frame types, and vice versa.
    assert(!pcmon_parse_status(gpu_frame, &not_stats));
    assert(!pcmon_parse_proc_list(gpu_frame, &pl));
    assert(!pcmon_parse_port_list(gpu_frame, &po));
    assert(!pcmon_parse_gpu(proc_frame, &g));
    assert(!pcmon_parse_gpu(port_frame, &g));

    pcmon_gpu_t gempty;
    assert(pcmon_parse_gpu("$PCGPU,v=,n=#", &gempty));
    assert(gempty.valid && !gempty.present);

    pcmon_gpu_t gout;
    pcmon_gpu_reset(&gout);
    gout.valid = true;
    gout.present = true;
    snprintf(gout.vendor, sizeof(gout.vendor), "nvidia");
    snprintf(gout.name, sizeof(gout.name), "RTX 3050");
    gout.has_util = true;
    gout.util = 45.5f;
    gout.has_vram = true;
    gout.vram_used_mb = 2048.0f;
    gout.vram_total_mb = 6144.0f;
    gout.has_temp = true;
    gout.temp = 55.0f;
    char gbuf[PCMON_LINE_MAX];
    assert(pcmon_format_gpu(&gout, gbuf, sizeof(gbuf)) > 0);
    pcmon_gpu_t gin;
    assert(pcmon_parse_gpu(gbuf, &gin));
    assert(gin.present && strcmp(gin.name, "RTX 3050") == 0);
    assert(gin.has_util && gin.util > 45.4f && gin.util < 45.6f);
    assert(gin.has_vram && gin.vram_total_mb > 6143.5f);
    assert(gin.has_temp && gin.temp > 54.9f);
    assert(!gin.has_fan && !gin.has_power);

    const char *ark_frame =
        "$PCARK,err=0,t=Coding Plan,plan=Pro,name=闫玉良,"
        "hq=25000.0,hu=1234.5,hr=16200,"
        "wq=87500.0,wu=500.0,wr=432000,"
        "mq=250000.0,mu=12000.0,mr=1209600#\n";
    pcmon_ark_t ark;
    assert(pcmon_parse_ark(ark_frame, &ark));
    assert(ark.valid && ark.err == 0);
    assert(strcmp(ark.title, "Coding Plan") == 0);
    assert(strcmp(ark.plan, "Pro") == 0);
    assert(strcmp(ark.name, "闫玉良") == 0);
    assert(ark.has_win[0] && ark.has_win[1] && ark.has_win[2]);
    assert(ark.quota[0] > 24999.0f && ark.used[0] > 1234.4f && ark.used[0] < 1234.6f);
    assert(ark.reset_s[0] == 16200);
    assert(ark.quota[1] > 87499.0f && ark.used[1] > 499.9f && ark.reset_s[1] == 432000);
    assert(ark.quota[2] > 249999.0f && ark.used[2] > 11999.0f && ark.reset_s[2] == 1209600);

    // Error frames still carry the tags; windows stay absent.
    pcmon_ark_t aerr;
    assert(pcmon_parse_ark("$PCARK,err=1,t=Coding Plan,name=闫玉良#", &aerr));
    assert(aerr.valid && aerr.err == 1);
    assert(strcmp(aerr.title, "Coding Plan") == 0);
    assert(strcmp(aerr.name, "闫玉良") == 0);
    assert(!aerr.has_win[0] && !aerr.has_win[1] && !aerr.has_win[2]);

    // A title longer than the buffer is truncated, not overflowing.
    pcmon_ark_t along;
    assert(pcmon_parse_ark(
        "$PCARK,err=0,t=0123456789012345678901234567890123456789#", &along));
    assert(strlen(along.title) == PCMON_ARK_TITLE_MAX - 1);

    // An ark frame must not be mistaken for the other frame types, and vice versa.
    assert(!pcmon_parse_status(ark_frame, &not_stats));
    assert(!pcmon_parse_proc_list(ark_frame, &pl));
    assert(!pcmon_parse_port_list(ark_frame, &po));
    assert(!pcmon_parse_gpu(ark_frame, &g));
    assert(!pcmon_parse_ark(gpu_frame, &ark));
    assert(!pcmon_parse_ark(proc_frame, &ark));
    assert(!pcmon_parse_ark(port_frame, &ark));

    pcmon_ark_t aout;
    pcmon_ark_reset(&aout);
    aout.valid = true;
    snprintf(aout.title, sizeof(aout.title), "Coding Plan");
    snprintf(aout.plan, sizeof(aout.plan), "Pro");
    snprintf(aout.name, sizeof(aout.name), "闫玉良");
    aout.has_win[0] = true;
    aout.quota[0] = 25000.0f;
    aout.used[0] = 1234.5f;
    aout.reset_s[0] = 16200;
    aout.has_win[2] = true;
    aout.quota[2] = 250000.0f;
    aout.used[2] = 12000.0f;
    aout.reset_s[2] = 1209600;
    char abuf[PCMON_LINE_MAX];
    assert(pcmon_format_ark(&aout, abuf, sizeof(abuf)) > 0);
    pcmon_ark_t ain;
    assert(pcmon_parse_ark(abuf, &ain));
    assert(ain.valid && ain.err == 0);
    assert(strcmp(ain.title, "Coding Plan") == 0);
    assert(strcmp(ain.plan, "Pro") == 0);
    assert(strcmp(ain.name, "闫玉良") == 0);
    assert(ain.has_win[0] && !ain.has_win[1] && ain.has_win[2]);
    assert(ain.quota[0] > 24999.0f && ain.used[0] > 1234.4f && ain.used[0] < 1234.6f);
    assert(ain.reset_s[0] == 16200);
    assert(ain.quota[2] > 249999.0f && ain.reset_s[2] == 1209600);

    // ---- TRAE status frames ($TSTAT) ----
    pcmon_trae_t trae;
    assert(!pcmon_parse_trae("plain text no frame", &trae));
    assert(!pcmon_parse_trae("$TAPRV,id=1,t=x#", &trae));

    const char *trae_full = "$TSTAT,st=code,task=12,rate=85,cm=42#\n";
    assert(pcmon_parse_trae(trae_full, &trae));
    assert(trae.valid && trae.st == PCMON_TRAE_CODE);
    assert(trae.has_task && trae.task == 12);
    assert(trae.has_rate && trae.rate == 85);
    assert(trae.has_cm && trae.cm == 42);

    // Partial frames only set the fields they carry.
    assert(pcmon_parse_trae("$TSTAT,st=think#", &trae));
    assert(trae.valid && trae.st == PCMON_TRAE_THINK);
    assert(!trae.has_task && !trae.has_rate && !trae.has_cm);

    // An empty state value still parses, defaulting to idle (the bridge always
    // sends at least st=.., so a bare "$TSTAT#" is not part of the protocol).
    assert(pcmon_parse_trae("$TSTAT,st=#", &trae));
    assert(trae.valid && trae.st == PCMON_TRAE_IDLE);
    assert(!trae.has_task && !trae.has_rate && !trae.has_cm);

    // Unknown state strings fall back to idle.
    assert(pcmon_parse_trae("$TSTAT,st=xyz,task=1#", &trae));
    assert(trae.valid && trae.st == PCMON_TRAE_IDLE && trae.has_task);

    // Every state keyword maps to its enum value.
    assert(pcmon_parse_trae("$TSTAT,st=build#", &trae) && trae.st == PCMON_TRAE_BUILD);
    assert(pcmon_parse_trae("$TSTAT,st=pass#", &trae) && trae.st == PCMON_TRAE_PASS);
    assert(pcmon_parse_trae("$TSTAT,st=fail#", &trae) && trae.st == PCMON_TRAE_FAIL);

    // A $TSTAT frame is not mistaken for other frame types.
    pcmon_stats_t not_stats2;
    assert(!pcmon_parse_status(trae_full, &not_stats2));
    assert(!pcmon_parse_ark(trae_full, &ark));

    // ---- TRAE approval frames ($TAPRV) ----
    pcmon_trae_aprv_t ap;
    assert(!pcmon_parse_trae_aprv("$TSTAT,st=code#", &ap));
    assert(!pcmon_parse_trae_aprv("no frame here", &ap));

    // clean_cmd turns ',' into ';' and drops '#', so the value may hold both,
    // plus '=' (the parser takes the first '=' as the key/value separator).
    assert(pcmon_parse_trae_aprv("$TAPRV,id=17,t=pytest; lr=0.01; 中文参数#", &ap));
    assert(ap.valid && ap.id == 17);
    assert(strcmp(ap.cmd, "pytest; lr=0.01; 中文参数") == 0);

    // A command longer than the buffer is truncated, not overflowed.
    char long_cmd[131];
    memset(long_cmd, 'a', sizeof(long_cmd) - 1);
    long_cmd[sizeof(long_cmd) - 1] = '\0';
    char long_frame[192];
    snprintf(long_frame, sizeof(long_frame), "$TAPRV,id=9,t=%s#", long_cmd);
    assert(pcmon_parse_trae_aprv(long_frame, &ap));
    assert(ap.valid && ap.id == 9);
    assert(strlen(ap.cmd) == PCMON_TRAE_CMD_MAX - 1);

    // ---- Device button frames ($TBTN) ----
    char tbtn[32];
    assert(pcmon_format_trae_btn(42, true, tbtn, sizeof(tbtn)) > 0);
    assert(strcmp(tbtn, "$TBTN,id=42,r=allow#\n") == 0);
    assert(pcmon_format_trae_btn(-3, false, tbtn, sizeof(tbtn)) > 0);
    assert(strcmp(tbtn, "$TBTN,id=-3,r=deny#\n") == 0);

    assert(pcmon_level(10.0f) == 0);
    assert(pcmon_level(75.0f) == 1);
    assert(pcmon_level(95.0f) == 2);

    puts("test_pcmon_protocol: OK");
    return 0;
}
