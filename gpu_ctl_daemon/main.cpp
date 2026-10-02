// gpu_ctl_daemon — root-privileged GPU control service. Applies
// GpuCtlConfigPOD (power cap, fan curve, clock offsets, VF curve, ...) to
// AMD sysfs / Nvidia NVML+NvAPI, over a Unix socket (see
// include/ipc/gpu_ctl_shm.h for the wire contract). Runs as a systemd
// service (packaging/systemd/vitals-gpud.service) — foreground, no
// daemonize/double-fork (unlike ram_oc_daemon: systemd supervises this
// one), and never spawned or elevated by `vitals` itself (see
// src/gpu_ctl/gpu_ctl_ipc.cpp, which only ever pings).
#include "amd_backend.h"
#include "config_store.h"
#include "ipc/gpu_ctl_shm.h"
#include "nvidia_backend.h"
#include "nvidia_nvapi_shim.h"
#include "pending_apply.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <grp.h>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr uint32_t kDefaultRevertTimeoutSec = 10; // more conservative than LACT's 5s default (docs/CONFIG.md)

struct GpuEntry {
    int32_t vendor = GPUCTL_VENDOR_AMD;
    AmdGpuHandle amd;
    NvidiaGpuHandle nvidia;
    std::string pciSlot;
    GpuCtlConfigPOD lastApplied{}; // what ResetToDefault must undo for state the backends can't read back (NvAPI)
};

std::vector<GpuEntry> g_gpus;
bool g_shutdownRequested = false;

void DiscoverGpus() {
    g_gpus.clear();
    std::vector<AmdGpuHandle> amdGpus;
    if (AmdDiscoverGpus(amdGpus)) {
        for (auto& h : amdGpus) {
            GpuEntry e; e.vendor = GPUCTL_VENDOR_AMD; e.amd = h; e.pciSlot = h.pciSlot;
            g_gpus.push_back(std::move(e));
        }
    }
    std::vector<NvidiaGpuHandle> nvGpus;
    if (NvidiaDiscoverGpus(nvGpus)) {
        for (auto& h : nvGpus) {
            GpuEntry e; e.vendor = GPUCTL_VENDOR_NVIDIA; e.nvidia = h; e.pciSlot = h.pciSlot;
            g_gpus.push_back(std::move(e));
        }
    }
    syslog(LOG_INFO, "gpu_ctl_daemon: discovered %zu GPU(s)", g_gpus.size());
}

bool GetCaps(int index, GpuCtlCapsPOD* out) {
    if (index < 0 || index >= (int)g_gpus.size()) return false;
    const GpuEntry& e = g_gpus[index];
    bool ok = e.vendor == GPUCTL_VENDOR_AMD ? AmdGetCaps(e.amd, out) : NvidiaGetCaps(e.nvidia, out);
    if (ok && e.vendor == GPUCTL_VENDOR_NVIDIA) out->supportsNvidiaVfCurve = NvApi().Loaded();
    return ok;
}

// Applies cfg to hardware. For Nvidia, NVML covers power/fan/plain clock
// offsets (nvidia_backend.cpp); voltage boost and the VF-curve editor go
// through the separate, explicitly experimental NvAPI shim — a failure
// there is logged but never aborts the rest of the config (see class
// comment in nvidia_nvapi_shim.h).
bool ApplyConfig(int index, GpuCtlConfigPOD cfg, std::string* err) {
    if (index < 0 || index >= (int)g_gpus.size()) { if (err) *err = "no such GPU"; return false; }
    GpuEntry& e = g_gpus[index];
    gpuctl_config_sanitize(&cfg); // single choke point: every apply path goes through here
    e.lastApplied = cfg;

    if (e.vendor == GPUCTL_VENDOR_AMD) return AmdApplyConfig(e.amd, cfg, err);

    if (!NvidiaApplyConfig(e.nvidia, cfg, err)) return false;

    if (cfg.clocks.voltageBoostPct != GPUCTL_UNSET && NvApi().Loaded()) {
        NvApiGpuHandle* gpu = NvApi().FindMatchingGpu(e.nvidia.pciBus);
        std::string nvErr;
        if (!gpu || !NvApi().SetVoltageBoostPercent(gpu, (uint8_t)std::clamp(cfg.clocks.voltageBoostPct, 0, 100), &nvErr)) {
            syslog(LOG_WARNING, "gpu_ctl_daemon: Nvidia voltage boost (experimental) failed: %s",
                   nvErr.empty() ? "no matching NvAPI handle" : nvErr.c_str());
        }
    }
    if (cfg.clocks.nvidiaVfCurveCount > 0 && NvApi().Loaded()) {
        NvApiGpuHandle* gpu = NvApi().FindMatchingGpu(e.nvidia.pciBus);
        std::string nvErr;
        if (!gpu || !NvApi().SetVfCurveOffsets(gpu, cfg.clocks.nvidiaVfCurve, cfg.clocks.nvidiaVfCurveCount, &nvErr)) {
            syslog(LOG_WARNING, "gpu_ctl_daemon: Nvidia VF-curve (experimental) failed: %s",
                   nvErr.empty() ? "no matching NvAPI handle" : nvErr.c_str());
        }
    }
    return true;
}

void ResetToDefault(int index) {
    if (index < 0 || index >= (int)g_gpus.size()) return;
    GpuEntry& e = g_gpus[index];
    if (e.vendor == GPUCTL_VENDOR_AMD) { AmdResetToDefault(e.amd); return; }
    NvidiaResetToDefault(e.nvidia);
    // NVML has no reset for the NvAPI-only knobs: undo what we last applied.
    NvApiGpuHandle* gpu = NvApi().Loaded() ? NvApi().FindMatchingGpu(e.nvidia.pciBus) : nullptr;
    if (gpu) {
        NvApi().SetVoltageBoostPercent(gpu, 0, nullptr);
        GpuCtlClocksPOD& c = e.lastApplied.clocks;
        if (c.nvidiaVfCurveCount > 0) {
            for (uint32_t i = 0; i < c.nvidiaVfCurveCount; ++i) c.nvidiaVfCurve[i].v.freqOffsetKhz = 0;
            NvApi().SetVfCurveOffsets(gpu, c.nvidiaVfCurve, c.nvidiaVfCurveCount, nullptr);
        }
    }
    gpuctl_config_init_unset(&e.lastApplied);
}

// Backends leave unset fields untouched, so "revert to confirmed" must start
// from hardware defaults — otherwise anything only the rejected config set
// (e.g. a power cap the confirmed config never touched) would stick.
void RevertTo(int index, bool hasConfirmed, const GpuCtlConfigPOD& confirmed) {
    ResetToDefault(index);
    if (hasConfirmed) ApplyConfig(index, confirmed, nullptr);
}

int FindIndexBySlot(const std::string& pciSlot) {
    for (size_t i = 0; i < g_gpus.size(); ++i) if (g_gpus[i].pciSlot == pciSlot) return (int)i;
    return -1;
}

// ---- socket plumbing (same shape as ram_oc_helper/daemon_main.c) --------

int CreateSocket() {
    unlink(VITALS_GPUCTL_SOCKET_PATH);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) { perror("socket"); exit(1); }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, VITALS_GPUCTL_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0) { perror("bind"); exit(1); }

    // 0660 + `vitals-gpu` group: unlike ram_oc_daemon's deliberately-0666
    // socket (read-only + rate-limited benchmark, no privileged side
    // effects), this socket performs root writes — group membership is the
    // access control (see scripts/install-daemons.sh, which creates the group
    // and adds the invoking user to it).
    chmod(VITALS_GPUCTL_SOCKET_PATH, 0660);
    struct group* g = getgrnam("vitals-gpu");
    if (g && chown(VITALS_GPUCTL_SOCKET_PATH, (uid_t)-1, g->gr_gid) != 0) {
        syslog(LOG_WARNING, "gpu_ctl_daemon: chown socket to vitals-gpu failed: %s", strerror(errno));
    } else if (!g) {
        syslog(LOG_WARNING, "gpu_ctl_daemon: 'vitals-gpu' group not found — socket stays root-only");
    }

    if (listen(fd, 8) != 0) { perror("listen"); exit(1); }
    return fd;
}

// Client gone / pipe broken mid-response is routine (not every caller waits
// for a reply, e.g. after REVERT) — nothing to do but drop it, same as the
// existing write()-return-ignoring precedent in ram_oc_helper/daemon_main.c.
void SendBytes(int fd, const void* data, size_t n) {
    if (send(fd, data, n, MSG_NOSIGNAL) < 0) { /* client gone; nothing to do */ }
}

void SendStatus(int fd, bool ok, const std::string& err) {
    GpuCtlStatusResponsePOD resp{};
    resp.ok = ok ? 1 : 0;
    std::strncpy(resp.error, err.c_str(), sizeof(resp.error) - 1);
    SendBytes(fd, &resp, sizeof(resp));
}

template <typename T>
bool ReadPayload(int fd, T* out) {
    // MSG_WAITALL: a stream socket may hand a ~1KB POD over in pieces.
    ssize_t n = recv(fd, out, sizeof(T), MSG_WAITALL);
    return n == (ssize_t)sizeof(T);
}

void HandleClient(int fd) {
    timeval tv{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int32_t cmd = 0;
    if (!ReadPayload(fd, &cmd)) { close(fd); return; }

    switch (cmd) {
        case GPUCTL_CMD_PING: {
            uint8_t ok = 1;
            SendBytes(fd, &ok, sizeof(ok));
            break;
        }
        case GPUCTL_CMD_LIST_GPUS: {
            GpuCtlListGpusResponsePOD resp{};
            resp.ok = 1;
            resp.gpuCount = (uint32_t)std::min(g_gpus.size(), (size_t)GPUCTL_MAX_GPUS);
            for (uint32_t i = 0; i < resp.gpuCount; ++i) GetCaps((int)i, &resp.gpus[i]);
            SendBytes(fd, &resp, sizeof(resp));
            break;
        }
        case GPUCTL_CMD_GET_CAPS: {
            GpuCtlGpuIndexRequestPOD req{};
            GpuCtlGetCapsResponsePOD resp{};
            if (ReadPayload(fd, &req) && GetCaps(req.gpuIndex, &resp.caps)) resp.ok = 1;
            else std::strncpy(resp.error, "no such GPU", sizeof(resp.error) - 1);
            SendBytes(fd, &resp, sizeof(resp));
            break;
        }
        case GPUCTL_CMD_GET_CONFIG: {
            GpuCtlGpuIndexRequestPOD req{};
            GpuCtlGetConfigResponsePOD resp{};
            if (ReadPayload(fd, &req) && req.gpuIndex >= 0 && req.gpuIndex < (int)g_gpus.size()) {
                resp.ok = 1;
                // No confirmed config yet: return an all-unset POD so the
                // client's sliders default to "no change" rather than 0.
                if (!PendingApply::GetConfirmed(g_gpus[req.gpuIndex].pciSlot, &resp.config))
                    gpuctl_config_init_unset(&resp.config);
            } else {
                std::strncpy(resp.error, "no such GPU", sizeof(resp.error) - 1);
            }
            SendBytes(fd, &resp, sizeof(resp));
            break;
        }
        case GPUCTL_CMD_SET_CONFIG: {
            GpuCtlSetConfigRequestPOD req{};
            GpuCtlSetConfigResponsePOD resp{};
            if (!ReadPayload(fd, &req) || req.gpuIndex < 0 || req.gpuIndex >= (int)g_gpus.size()) {
                std::strncpy(resp.error, "no such GPU", sizeof(resp.error) - 1);
                SendBytes(fd, &resp, sizeof(resp));
                break;
            }
            std::string err;
            gpuctl_config_sanitize(&req.config);
            if (ApplyConfig(req.gpuIndex, req.config, &err)) {
                PendingApply::Arm(g_gpus[req.gpuIndex].pciSlot, kDefaultRevertTimeoutSec, req.config);
                resp.ok = 1;
                resp.revertDeadlineSec = kDefaultRevertTimeoutSec;
            } else {
                std::strncpy(resp.error, err.c_str(), sizeof(resp.error) - 1);
            }
            SendBytes(fd, &resp, sizeof(resp));
            break;
        }
        case GPUCTL_CMD_CONFIRM: {
            GpuCtlConfirmRequestPOD req{};
            // Persists the config the daemon itself armed on SET_CONFIG, not
            // req.config: the payload is kept only for wire compatibility.
            // Trusting it would let a late CONFIRM (after the timer already
            // reverted) — or a forged one — persist a config that was never
            // confirmed live, to be reapplied as root on next boot. Mirrors
            // LACT's confirm, which just cancels its own pending timer.
            if (ReadPayload(fd, &req) && req.gpuIndex >= 0 && req.gpuIndex < (int)g_gpus.size()) {
                const std::string& slot = g_gpus[req.gpuIndex].pciSlot;
                GpuCtlConfigPOD confirmed;
                if (!PendingApply::Confirm(slot, &confirmed)) {
                    SendStatus(fd, false, "nothing to confirm (already reverted?)");
                    break;
                }
                auto all = ConfigStore::LoadAll();
                all[slot] = confirmed;
                ConfigStore::SaveAll(all);
                SendStatus(fd, true, "");
                break;
            }
            SendStatus(fd, false, "no such GPU");
            break;
        }
        case GPUCTL_CMD_REVERT: {
            GpuCtlGpuIndexRequestPOD req{};
            if (ReadPayload(fd, &req) && req.gpuIndex >= 0 && req.gpuIndex < (int)g_gpus.size()) {
                GpuCtlConfigPOD cfg;
                const std::string& slot = g_gpus[req.gpuIndex].pciSlot;
                PendingApply::Cancel(slot);
                bool has = PendingApply::GetConfirmed(slot, &cfg);
                RevertTo(req.gpuIndex, has, cfg);
                SendStatus(fd, true, "");
                break;
            }
            SendStatus(fd, false, "no such GPU");
            break;
        }
        case GPUCTL_CMD_RESET_DEFAULT: {
            GpuCtlGpuIndexRequestPOD req{};
            if (ReadPayload(fd, &req) && req.gpuIndex >= 0 && req.gpuIndex < (int)g_gpus.size()) {
                // Also drop the confirmed/persisted config, or the UI keeps
                // showing it and the next daemon start silently reapplies it.
                const std::string& slot = g_gpus[req.gpuIndex].pciSlot;
                ResetToDefault(req.gpuIndex);
                PendingApply::Forget(slot);
                auto all = ConfigStore::LoadAll();
                if (all.erase(slot)) ConfigStore::SaveAll(all);
                SendStatus(fd, true, "");
                break;
            }
            SendStatus(fd, false, "no such GPU");
            break;
        }
        case GPUCTL_CMD_SHUTDOWN: {
            // Ack first, then let main()'s loop exit cleanly (return 0) once
            // this handler returns — Restart=on-failure in the systemd unit
            // only restarts on a non-zero exit, so this leaves the daemon
            // stopped until the user (or systemd) starts it again.
            SendStatus(fd, true, "");
            g_shutdownRequested = true;
            break;
        }
        default:
            break;
    }
    close(fd);
}

} // namespace

int main() {
    openlog("gpu_ctl_daemon", LOG_PID, LOG_DAEMON);
    signal(SIGPIPE, SIG_IGN);

    if (geteuid() != 0) {
        syslog(LOG_ERR, "gpu_ctl_daemon must run as root (systemd unit should set User=root)");
        return 1;
    }

    DiscoverGpus();
    NvApi(); // best-effort lazy init; Loaded()==false is fine (AMD-only systems, older drivers)

    for (auto& [slot, cfg] : ConfigStore::LoadAll()) {
        PendingApply::SeedConfirmed(slot, cfg);
        int idx = FindIndexBySlot(slot);
        if (idx >= 0) {
            std::string err;
            if (!ApplyConfig(idx, cfg, &err))
                syslog(LOG_WARNING, "gpu_ctl_daemon: failed to reapply persisted config for %s: %s", slot.c_str(), err.c_str());
        }
    }

    int listenFd = CreateSocket();
    syslog(LOG_INFO, "gpu_ctl_daemon: listening on %s", VITALS_GPUCTL_SOCKET_PATH);

    for (;;) {
        pollfd pfd{listenFd, POLLIN, 0};
        int pret = poll(&pfd, 1, 200);
        if (pret > 0 && (pfd.revents & POLLIN)) {
            int clientFd = accept(listenFd, nullptr, nullptr);
            if (clientFd >= 0) HandleClient(clientFd);
        }
        if (g_shutdownRequested) {
            syslog(LOG_INFO, "gpu_ctl_daemon: shutdown requested via IPC, exiting cleanly");
            break;
        }
        PendingApply::CheckExpirations([](const std::string& slot, bool hasConfirmed, const GpuCtlConfigPOD& cfg) {
            int idx = FindIndexBySlot(slot);
            if (idx < 0) return;
            syslog(LOG_INFO, "gpu_ctl_daemon: %s — no confirmation received, reverting settings", slot.c_str());
            RevertTo(idx, hasConfirmed, cfg);
        });
    }
    unlink(VITALS_GPUCTL_SOCKET_PATH);
    return 0;
}
