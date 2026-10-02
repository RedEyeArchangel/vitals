#include "dram_oc/daemon_ipc.h"

#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

bool TryPing(const char* path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    timeval tv{1, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0) { close(fd); return false; }

    int32_t cmd = CMD_PING;
    if (send(fd, &cmd, sizeof(cmd), MSG_NOSIGNAL) != (ssize_t)sizeof(cmd)) { close(fd); return false; }

    uint8_t resp = 0;
    ssize_t n = read(fd, &resp, sizeof(resp));
    close(fd);
    return n == 1 && resp == 1;
}

// Doesn't assume which path the (possibly already-running, possibly
// differently-privileged) daemon bound to — just checks both.
std::string FindLiveSocketPath() {
    if (TryPing(VITALS_SOCKET_RUN_PATH)) return VITALS_SOCKET_RUN_PATH;
    if (TryPing(VITALS_SOCKET_TMP_PATH)) return VITALS_SOCKET_TMP_PATH;
    return "";
}

// SMU/DRAM registers under /sys/kernel/ryzen_smu_drv/ are root-only, so a
// daemon started unprivileged reads back zeroed telemetry forever after
// (backend_is_supported() only checks the file exists, not that it's
// readable) — it never retries or upgrades itself. Get the uid of whoever
// is currently answering `path`, via SO_PEERCRED on a throwaway connection.
uid_t PeerUid(const char* path, pid_t* outPid) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return (uid_t)-1;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    uid_t uid = (uid_t)-1;
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0) {
        struct ucred cred{};
        socklen_t len = sizeof(cred);
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
            uid = cred.uid;
            if (outPid) *outPid = cred.pid;
        }
    }
    close(fd);
    return uid;
}

// Locates ram_oc_daemon alongside this executable (via /proc/self/exe), so
// it's found regardless of cwd or how vitals was launched.
std::string FindDaemonPath() {
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return "";
    exe[n] = '\0';
    std::string path(exe);
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return "";
    std::string candidate = path.substr(0, slash + 1) + "ram_oc_daemon";
    if (access(candidate.c_str(), X_OK) != 0) return "";
    return candidate;
}

struct DaemonHandle {
    const volatile VitalsTelemetryFrame* frame = nullptr; // PROT_READ mmap, or null if unavailable
};

// Root, or ourselves: the only peers whose telemetry we accept. Root is
// already all-powerful, and only root can create /run's socket at all — so a
// non-root vitals reuses a daemon a previous `sudo vitals` left behind
// instead of spawning its own, which would die on the root-owned shm name.
bool TrustedUid(uid_t uid) { return uid == geteuid() || uid == 0; }

// shm attach happens once a trusted daemon is found, and again after it
// restarts — a new daemon unlinks and recreates the segment, so the old
// mapping would show frozen data forever (see GetDaemonTelemetryFrame).
DaemonHandle g_handle;
bool g_attached = false;

DaemonHandle Attach() {
    DaemonHandle out;
    if (FindTrustedDaemon().empty()) return out;

    int fd = shm_open(VITALS_SHM_NAME, O_RDONLY, 0);
    if (fd < 0) return out;

    // Guard against a stale daemon from a previous vitals version still
    // holding /vitals_ram_oc sized for an older, smaller frame layout
    // (e.g. this build's daemon hasn't restarted yet) — mmap-ing our
    // larger struct over a shorter segment would SIGBUS on first touch
    // of the tail past the object's real size.
    struct stat st;
    if (fstat(fd, &st) != 0 || (size_t)st.st_size < sizeof(VitalsTelemetryFrame)) {
        close(fd);
        return out;
    }
    // The shm name is a fixed, predictable global name in world-writable
    // /dev/shm — anyone could have pre-created it before our (UID-verified,
    // see FindTrustedDaemon) daemon claimed it. Refuse to trust content
    // we didn't write ourselves.
    if (!TrustedUid(st.st_uid)) {
        close(fd);
        return out;
    }

    void* p = mmap(nullptr, sizeof(VitalsTelemetryFrame), PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return out;
    out.frame = (const volatile VitalsTelemetryFrame*)p;
    return out;
}

const DaemonHandle& GetHandle() {
    if (!g_attached) { g_handle = Attach(); g_attached = g_handle.frame != nullptr; }
    return g_handle;
}

void Detach() {
    if (g_handle.frame) munmap((void*)g_handle.frame, sizeof(VitalsTelemetryFrame));
    g_handle = DaemonHandle{};
    g_attached = false;
}

} // namespace

std::string FindTrustedDaemon() {
    // Never spawns, kills or elevates anything: vitals only *connects* to a
    // daemon the user started themselves (Settings → Daemons shows how).
    // Trust check: the socket path is a fixed, predictable name, so only a
    // peer running as us or as root is believed (see TrustedUid).
    std::string path = FindLiveSocketPath();
    if (path.empty()) return "";
    return TrustedUid(PeerUid(path.c_str(), nullptr)) ? path : "";
}

const volatile VitalsTelemetryFrame* GetDaemonTelemetryFrame() {
    // Re-checked on every call (callers throttle to ~1.5s): attach once a
    // daemon appears, drop the mapping once it's gone so a stopped daemon
    // doesn't leave frozen numbers on screen.
    if (g_attached && FindTrustedDaemon().empty()) Detach();
    return GetHandle().frame;
}

std::string RamOcDaemonPath() { return FindDaemonPath(); }

DaemonStatus GetRamOcDaemonStatus() {
    DaemonStatus st;
    std::string path = FindLiveSocketPath();
    if (path.empty()) return st;
    pid_t pid = -1;
    uid_t uid = PeerUid(path.c_str(), &pid);
    st.running = TrustedUid(uid);
    st.asRoot = uid == 0;
    st.ownedByUs = uid == geteuid();
    return st;
}

bool RunDaemonBenchmark(VitalsBenchResultPOD& out) {
    std::string sockPath = FindTrustedDaemon();
    if (sockPath.empty()) return false;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0) { close(fd); return false; }

    // Generous: bench_run() blocks ~2-4s on real hardware, longer under load.
    timeval tv{30, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int32_t cmd = CMD_RUN_BENCHMARK;
    if (send(fd, &cmd, sizeof(cmd), MSG_NOSIGNAL) != (ssize_t)sizeof(cmd)) { close(fd); return false; }

    ssize_t n = recv(fd, &out, sizeof(out), MSG_WAITALL);
    close(fd);
    return n == (ssize_t)sizeof(out);
}
