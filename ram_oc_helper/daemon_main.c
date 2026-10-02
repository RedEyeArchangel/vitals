#define _GNU_SOURCE /* struct ucred / SO_PEERCRED (see TryPingPeer below) */

/* ram_oc_daemon — persistent GPLv3 helper process. Links the vendored
 * ram_oc/ sources (backend.c/dram.c/pm_table.c/util.c/bench.c) so that GPL
 * code never gets compiled into `vitals` itself (see include/ipc/ram_oc_shm.h
 * for the neutral wire contract, and src/dram_oc.cpp for the vitals-side
 * client). This whole binary is correctly GPLv3, since it's built from GPL
 * sources; vitals proper is not.
 *
 * Refreshes DRAM/SMU telemetry into POSIX shared memory (VITALS_SHM_NAME,
 * versioned — see ram_oc_shm.h) roughly once a second, and serves benchmark
 * requests over a Unix domain
 * socket. Daemonizes on startup and keeps running after the launching
 * vitals process exits — same lifetime as e.g. a systemd-managed sensor
 * daemon. Self-arbitrates via a CMD_PING probe so a second `vitals` launch
 * doesn't spawn a duplicate.
 */
#include "ipc/ram_oc_shm.h"

#include "backend.h"
#include "bench.h"
#include "types.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static void CopyStr(char* dst, size_t dstSize, const char* src) {
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

static const char* MemTypeName(mem_type_t t) {
    switch (t) {
        case MEM_DDR4:   return "DDR4";
        case MEM_DDR5:   return "DDR5";
        case MEM_LPDDR4: return "LPDDR4";
        case MEM_LPDDR5: return "LPDDR5";
        default:         return "Unknown";
    }
}

static const char* RankName(mem_rank_t r) {
    switch (r) {
        case RANK_DR: return "DR";
        case RANK_QR: return "QR";
        default:      return "SR";
    }
}

/* ---- daemonize: classic double-fork ---- */

static void Daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) exit(1);
    if (pid > 0) exit(0); /* first parent exits immediately */

    if (setsid() < 0) exit(1);

    pid = fork();
    if (pid < 0) exit(1);
    if (pid > 0) exit(0); /* second parent exits */

    /* grandchild: the actual daemon */
    if (chdir("/") != 0) { /* non-fatal */ }
    umask(0022);
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO) close(devnull);
    }
}

/* ---- socket path + liveness probe ---- */

static void ChooseSocketPath(char* out, size_t outSize) {
    if (access("/run", W_OK) == 0) CopyStr(out, outSize, VITALS_SOCKET_RUN_PATH);
    else CopyStr(out, outSize, VITALS_SOCKET_TMP_PATH);
}

/* Pings `path` and, if outUid is non-NULL, reports the peer's uid via
 * SO_PEERCRED. Doesn't judge trust itself — callers compare outUid against
 * their own geteuid(), same as the client-side check in daemon_ipc.cpp,
 * since the socket path is fixed/predictable in a world-writable directory
 * and anything could be squatting it. */
static int TryPingPeer(const char* path, uid_t* outUid) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 0;

    struct timeval tv = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    CopyStr(addr.sun_path, sizeof(addr.sun_path), path);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) { close(fd); return 0; }

    if (outUid) {
        struct ucred cred;
        socklen_t len = sizeof(cred);
        *outUid = (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) ? cred.uid : (uid_t)-1;
    }

    int32_t cmd = CMD_PING;
    if (write(fd, &cmd, sizeof(cmd)) != (ssize_t)sizeof(cmd)) { close(fd); return 0; }

    uint8_t resp = 0;
    ssize_t n = read(fd, &resp, sizeof(resp));
    close(fd);
    return n == 1 && resp == 1;
}

static int TryPing(const char* path) {
    return TryPingPeer(path, NULL);
}

/* ---- shared memory ---- */

static VitalsTelemetryFrame* CreateShm(void) {
    /* Caller (main) has already confirmed no live daemon is answering PING,
     * so any existing segment is stale (a crashed previous daemon) — safe
     * to unlink and recreate fresh. */
    if (shm_unlink(VITALS_SHM_NAME) != 0 && errno != ENOENT) {
        /* Most likely a different uid owns the segment (squatted, or a
         * same-name daemon we don't have permission to remove) — surface it
         * via syslog since stdio is already /dev/null by this point. */
        syslog(LOG_WARNING, "shm_unlink(%s) failed: %s", VITALS_SHM_NAME, strerror(errno));
    }

    int fd = shm_open(VITALS_SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0644);
    if (fd < 0) {
        syslog(LOG_ERR, "shm_open(%s, O_CREAT|O_EXCL) failed: %s — likely still held by another user",
               VITALS_SHM_NAME, strerror(errno));
        perror("shm_open"); exit(1);
    }
    if (ftruncate(fd, sizeof(VitalsTelemetryFrame)) != 0) { perror("ftruncate"); exit(1); }
    fchmod(fd, 0644); /* readable by any local user — telemetry isn't sensitive */

    void* p = mmap(NULL, sizeof(VitalsTelemetryFrame), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) { perror("mmap"); exit(1); }

    memset(p, 0, sizeof(VitalsTelemetryFrame));
    return (VitalsTelemetryFrame*)p;
}

/* ---- socket server ---- */

static int CreateSocket(const char* path) {
    if (unlink(path) != 0 && errno != ENOENT) {
        syslog(LOG_WARNING, "unlink(%s) failed: %s", path, strerror(errno));
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) { perror("socket"); exit(1); }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    CopyStr(addr.sun_path, sizeof(addr.sun_path), path);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        syslog(LOG_ERR, "bind(%s) failed: %s — likely still held by another user", path, strerror(errno));
        perror("bind"); exit(1);
    }
    chmod(path, 0666); /* PING/RUN_BENCHMARK have no privileged side effects
                         * (RUN_BENCHMARK is rate-limited below to one in
                         * flight, so it isn't a resource-exhaustion vector
                         * either — see g_benchRunning). */
    if (listen(fd, 8) != 0) { perror("listen"); exit(1); }
    return fd;
}

typedef struct { int fd; } BenchThreadArg;

/* Only one benchmark runs at a time: CMD_RUN_BENCHMARK is reachable by any
 * local user (the socket is deliberately 0666), and bench_run() is a
 * multi-second, multi-gigabyte, all-core memory stress test — without this
 * cap, a client could spam CMD_RUN_BENCHMARK to spawn unbounded concurrent
 * instances of it as a resource-exhaustion DoS against a possibly-root
 * daemon. The UI only ever needs one result at a time anyway. */
static atomic_bool g_benchRunning = false;

static void* BenchmarkThread(void* argRaw) {
    BenchThreadArg* arg = (BenchThreadArg*)argRaw;
    int fd = arg->fd;
    free(arg);

    bench_results_t r;
    memset(&r, 0, sizeof(r));
    bench_run(&r); /* blocks ~2-4s, internally multi-threaded — this is why
                     * it runs on its own thread instead of the main loop */

    VitalsBenchResultPOD pod;
    pod.lat_l1_ns = (float)r.lat_l1_ns;
    pod.lat_l2_ns = (float)r.lat_l2_ns;
    pod.lat_l3_ns = (float)r.lat_l3_ns;
    pod.lat_dram_ns = (float)r.lat_dram_ns;
    pod.bw_read_mbs = (uint32_t)r.bw_read_mbs;
    pod.bw_write_mbs = (uint32_t)r.bw_write_mbs;
    pod.bw_copy_mbs = (uint32_t)r.bw_copy_mbs;

    if (send(fd, &pod, sizeof(pod), MSG_NOSIGNAL) < 0) { /* client gone; nothing to do but close below */ }
    close(fd);
    atomic_store(&g_benchRunning, false);
    return NULL;
}

static void HandleClient(int fd) {
    /* Cap how long a slow/hung client can occupy the (synchronous) command
     * read below, so it can't stall the telemetry refresh cadence. */
    struct timeval tv = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int32_t cmd = 0;
    ssize_t n = read(fd, &cmd, sizeof(cmd));
    if (n != (ssize_t)sizeof(cmd)) { close(fd); return; }

    if (cmd == CMD_PING) {
        uint8_t ok = 1;
        if (write(fd, &ok, sizeof(ok)) < 0) { /* client gone; nothing to do but close below */ }
        close(fd);
    } else if (cmd == CMD_RUN_BENCHMARK) {
        /* Enforce the one-in-flight cap documented at g_benchRunning: a
         * second request while one runs is just dropped (client sees EOF). */
        if (atomic_exchange(&g_benchRunning, true)) { close(fd); return; }
        BenchThreadArg* arg = (BenchThreadArg*)malloc(sizeof(BenchThreadArg));
        pthread_t th;
        if (arg) arg->fd = fd;
        if (arg && pthread_create(&th, NULL, BenchmarkThread, arg) == 0) {
            pthread_detach(th);
        } else {
            free(arg);
            close(fd);
            atomic_store(&g_benchRunning, false);
        }
    } else {
        close(fd);
    }
}

/* ---- telemetry refresh ---- */

static void RefreshTelemetry(VitalsTelemetryFrame* frame) {
    /* backend_read_summary() always fills the vendor-neutral fields (DIMM/SPD,
     * memory speed/type, per-core temp/usage/freq, RAPL package power) and
     * additionally fills the AMD-only ones when s.smu_supported is true —
     * never gate this call itself on backend_is_supported(). */
    system_summary_t s;
    backend_read_summary(&s);
    const smu_metrics_t* sm = &s.metrics;
    const dram_timings_t* d = &s.dram;

    vitals_shm_begin_write(frame);

    frame->supported = true;
    frame->smu_supported = s.smu_supported;
    CopyStr(frame->cpu_codename, sizeof(frame->cpu_codename), s.cpu.codename);
    CopyStr(frame->cpu_model, sizeof(frame->cpu_model), s.cpu.processor_name);
    CopyStr(frame->smu_version, sizeof(frame->smu_version), s.cpu.smu_version);
    CopyStr(frame->pm_table_version, sizeof(frame->pm_table_version), s.cpu.pm_table_version);
    CopyStr(frame->board_display_line, sizeof(frame->board_display_line), s.board.display_line);
    frame->memory_frequency = s.memory.frequency;
    CopyStr(frame->memory_type, sizeof(frame->memory_type), MemTypeName(s.memory.type));
    frame->package_power_w = sm->package_power_w;

    frame->ppt_w = sm->ppt_w; frame->vcore = sm->vcore; frame->vsoc = sm->vsoc;
    frame->vddp = sm->vddp; frame->vddg_ccd = sm->vddg_ccd; frame->vddg_iod = sm->vddg_iod;
    frame->vdd_misc = sm->vdd_misc; frame->cpu_vddio = sm->cpu_vddio;
    frame->mem_vdd = sm->mem_vdd; frame->mem_vddq = sm->mem_vddq; frame->mem_vpp = sm->mem_vpp;
    frame->vid = sm->vid;
    frame->bclk_mhz = sm->bclk_mhz; frame->fclk_mhz = sm->fclk_mhz;
    frame->uclk_mhz = sm->uclk_mhz; frame->mclk_mhz = sm->mclk_mhz;

    frame->has_tdie = sm->has_tdie; frame->tdie_c = sm->tdie_c;
    frame->has_tctl = sm->has_tctl; frame->tctl_c = sm->tctl_c;
    frame->has_tccd1 = sm->has_tccd1; frame->tccd1_c = sm->tccd1_c;
    frame->has_tccd2 = sm->has_tccd2; frame->tccd2_c = sm->tccd2_c;
    frame->has_iod_hotspot = sm->has_iod_hotspot; frame->iod_hotspot_c = sm->iod_hotspot_c;

    frame->core_voltage_count = (uint32_t)(sm->core_voltages_count < MAX_CPU_CORES ? sm->core_voltages_count : MAX_CPU_CORES);
    for (uint32_t i = 0; i < frame->core_voltage_count; ++i) frame->core_voltages[i] = sm->core_voltages[i];
    frame->core_temp_count = (uint32_t)(sm->core_temps_count < MAX_CPU_CORES ? sm->core_temps_count : MAX_CPU_CORES);
    for (uint32_t i = 0; i < frame->core_temp_count; ++i) frame->core_temps_c[i] = sm->core_temps_c[i];
    frame->core_usage_count = (uint32_t)(sm->core_usage_count < MAX_CPU_CORES ? sm->core_usage_count : MAX_CPU_CORES);
    for (uint32_t i = 0; i < frame->core_usage_count; ++i) frame->core_usage_pct[i] = sm->core_usage_pct[i];
    frame->core_freq_count = (uint32_t)(sm->core_freq_count < MAX_CPU_CORES ? sm->core_freq_count : MAX_CPU_CORES);
    for (uint32_t i = 0; i < frame->core_freq_count; ++i) frame->core_freq_mhz[i] = sm->core_freq_mhz[i];
    frame->spd_temp_count = (uint32_t)(sm->spd_temps_count < MAX_DIMM_MODULES ? sm->spd_temps_count : MAX_DIMM_MODULES);
    for (uint32_t i = 0; i < frame->spd_temp_count; ++i) frame->spd_temps_c[i] = sm->spd_temps_c[i];

    frame->module_count = (uint32_t)(s.module_count < MAX_DIMM_MODULES ? s.module_count : MAX_DIMM_MODULES);
    for (uint32_t i = 0; i < frame->module_count; ++i) {
        const memory_module_t* mod = &s.modules[i];
        VitalsDramModulePOD* out = &frame->modules[i];
        CopyStr(out->manufacturer, sizeof(out->manufacturer), mod->manufacturer);
        CopyStr(out->part_number, sizeof(out->part_number), mod->part_number);
        CopyStr(out->serial_number, sizeof(out->serial_number), mod->serial_number);
        CopyStr(out->capacity_display, sizeof(out->capacity_display), mod->capacity_display);
        CopyStr(out->slot_display, sizeof(out->slot_display), mod->slot_display);
        CopyStr(out->rank, sizeof(out->rank), RankName(mod->rank));
    }

    frame->fan_count = (uint32_t)(s.fan_count < MAX_FANS ? s.fan_count : MAX_FANS);
    for (uint32_t i = 0; i < frame->fan_count; ++i) {
        CopyStr(frame->fans[i].label, sizeof(frame->fans[i].label), s.fans[i].label);
        frame->fans[i].rpm = s.fans[i].rpm;
    }

    frame->tcl = d->tcl; frame->trcd_rd = d->trcd_rd; frame->trcd_wr = d->trcd_wr;
    frame->trp = d->trp; frame->tras = d->tras; frame->trc = d->trc;
    frame->trrds = d->trrds; frame->trrdl = d->trrdl; frame->tfaw = d->tfaw;
    frame->twr = d->twr; frame->tcwl = d->tcwl;
    frame->rtp = d->rtp; frame->wtrs = d->wtrs; frame->wtrl = d->wtrl;
    frame->rdwr = d->rdwr; frame->wrrd = d->wrrd;
    frame->rdrd_scl = d->rdrd_scl; frame->wrwr_scl = d->wrwr_scl;
    frame->rdrd_sc = d->rdrd_sc; frame->rdrd_sd = d->rdrd_sd; frame->rdrd_dd = d->rdrd_dd;
    frame->wrwr_sc = d->wrwr_sc; frame->wrwr_sd = d->wrwr_sd; frame->wrwr_dd = d->wrwr_dd;
    frame->refi = d->refi; frame->wrpre = d->wrpre; frame->rdpre = d->rdpre;
    frame->trc_page = d->trc_page; frame->mod = d->mod; frame->mod_pda = d->mod_pda;
    frame->mrd = d->mrd; frame->mrd_pda = d->mrd_pda;
    frame->stag = d->stag; frame->stag_sb = d->stag_sb; frame->cke = d->cke; frame->xp = d->xp;
    frame->phy_wrd = d->phy_wrd; frame->phy_wrl = d->phy_wrl; frame->phy_rdl = d->phy_rdl;
    frame->rfc = d->rfc; frame->rfc2 = d->rfc2; frame->rfcsb = d->rfcsb;
    frame->trefi_ns = d->trefi_ns; frame->trfc_ns = d->trfc_ns;
    frame->gdm_enabled = d->gdm_enabled; frame->power_down_enabled = d->power_down_enabled;
    CopyStr(frame->cmd2t, sizeof(frame->cmd2t), d->cmd2t);

    vitals_shm_end_write(frame);
}

/* ---- main loop ---- */

static void MainLoop(VitalsTelemetryFrame* frame, int listenFd) {
    time_t lastRefresh = 0;
    for (;;) {
        struct pollfd pfd;
        pfd.fd = listenFd;
        pfd.events = POLLIN;
        int pret = poll(&pfd, 1, 200); /* short so telemetry cadence stays close to ~1s */

        if (pret > 0 && (pfd.revents & POLLIN)) {
            int clientFd = accept(listenFd, NULL, NULL);
            if (clientFd >= 0) HandleClient(clientFd);
        }

        time_t now = time(NULL);
        if (now - lastRefresh >= 1) {
            RefreshTelemetry(frame);
            lastRefresh = now;
        }
    }
}

int main(int argc, char** argv) {
    /* --foreground: run under a supervisor (packaging/systemd/vitals-ramocd.service)
     * instead of double-forking — systemd tracks the main PID itself. */
    int foreground = argc > 1 && strcmp(argv[1], "--foreground") == 0;
    openlog("ram_oc_daemon", LOG_PID, LOG_DAEMON);
    char socketPath[128];
    ChooseSocketPath(socketPath, sizeof(socketPath));

    /* ponytail: this liveness probe + the create step below aren't atomic —
     * two `sudo ./vitals` launches within the same instant could both pass
     * this check. Acceptable for a desktop monitoring app; a lockfile with
     * flock() would close the race if it ever matters. */
    uid_t livePeerUid = (uid_t)-1;
    if (TryPingPeer(socketPath, &livePeerUid) && livePeerUid == geteuid()) {
        return 0; /* a live daemon we actually own already answers here */
    }
    /* Anything else answering is squatting the name (or a stale check from
     * before we dropped privileges) — fall through and reclaim it below. */

    if (!foreground) Daemonize();
    signal(SIGPIPE, SIG_IGN);

    VitalsTelemetryFrame* frame = CreateShm();
    int listenFd = CreateSocket(socketPath);

    MainLoop(frame, listenFd);
    return 0;
}
