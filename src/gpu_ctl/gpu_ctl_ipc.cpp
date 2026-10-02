#include "gpu_ctl/gpu_ctl_ipc.h"

#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

// Returns -1 on any failure to connect; caller treats that as "daemon not
// reachable" (matching DaemonReachable()'s contract — never spawns one).
int Connect() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    timeval tv{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, VITALS_GPUCTL_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0) { close(fd); return -1; }
    return fd;
}

template <typename Req, typename Resp>
bool Roundtrip(int32_t cmd, const Req* req, Resp* resp) {
    int fd = Connect();
    if (fd < 0) return false;
    // MSG_NOSIGNAL: a daemon that hung up mid-call must not SIGPIPE vitals.
    // MSG_WAITALL: LIST_GPUS' ~3.5KB reply can arrive in more than one chunk.
    bool ok = send(fd, &cmd, sizeof(cmd), MSG_NOSIGNAL) == (ssize_t)sizeof(cmd);
    if (ok && req) ok = send(fd, req, sizeof(Req), MSG_NOSIGNAL) == (ssize_t)sizeof(Req);
    if (ok) ok = recv(fd, resp, sizeof(Resp), MSG_WAITALL) == (ssize_t)sizeof(Resp);
    close(fd);
    return ok;
}

} // namespace

namespace GpuCtlIpc {

bool DaemonReachable() {
    int fd = Connect();
    if (fd < 0) return false;
    int32_t cmd = GPUCTL_CMD_PING;
    bool ok = send(fd, &cmd, sizeof(cmd), MSG_NOSIGNAL) == (ssize_t)sizeof(cmd);
    uint8_t resp = 0;
    ok = ok && read(fd, &resp, sizeof(resp)) == (ssize_t)sizeof(resp) && resp == 1;
    close(fd);
    return ok;
}

bool ListGpus(GpuCtlListGpusResponsePOD& out) {
    return Roundtrip<int, GpuCtlListGpusResponsePOD>(GPUCTL_CMD_LIST_GPUS, nullptr, &out) && out.ok;
}

bool GetCaps(int gpuIndex, GpuCtlCapsPOD& out) {
    GpuCtlGpuIndexRequestPOD req{gpuIndex};
    GpuCtlGetCapsResponsePOD resp{};
    if (!Roundtrip(GPUCTL_CMD_GET_CAPS, &req, &resp) || !resp.ok) return false;
    out = resp.caps;
    return true;
}

bool GetConfig(int gpuIndex, GpuCtlConfigPOD& out) {
    GpuCtlGpuIndexRequestPOD req{gpuIndex};
    GpuCtlGetConfigResponsePOD resp{};
    if (!Roundtrip(GPUCTL_CMD_GET_CONFIG, &req, &resp) || !resp.ok) return false;
    out = resp.config;
    return true;
}

bool SetConfig(int gpuIndex, const GpuCtlConfigPOD& cfg, uint32_t* deadlineSecOut, std::string* errOut) {
    GpuCtlSetConfigRequestPOD req{gpuIndex, cfg};
    GpuCtlSetConfigResponsePOD resp{};
    bool ok = Roundtrip(GPUCTL_CMD_SET_CONFIG, &req, &resp) && resp.ok;
    if (ok && deadlineSecOut) *deadlineSecOut = resp.revertDeadlineSec;
    if (!ok && errOut) *errOut = resp.error[0] ? resp.error : "gpu_ctl_daemon unreachable";
    return ok;
}

bool Confirm(int gpuIndex, const GpuCtlConfigPOD& cfg) {
    GpuCtlConfirmRequestPOD req{gpuIndex, cfg};
    GpuCtlStatusResponsePOD resp{};
    return Roundtrip(GPUCTL_CMD_CONFIRM, &req, &resp) && resp.ok;
}

bool Revert(int gpuIndex) {
    GpuCtlGpuIndexRequestPOD req{gpuIndex};
    GpuCtlStatusResponsePOD resp{};
    return Roundtrip(GPUCTL_CMD_REVERT, &req, &resp) && resp.ok;
}

bool ResetToDefault(int gpuIndex) {
    GpuCtlGpuIndexRequestPOD req{gpuIndex};
    GpuCtlStatusResponsePOD resp{};
    return Roundtrip(GPUCTL_CMD_RESET_DEFAULT, &req, &resp) && resp.ok;
}

bool Shutdown() {
    GpuCtlStatusResponsePOD resp{};
    return Roundtrip<int, GpuCtlStatusResponsePOD>(GPUCTL_CMD_SHUTDOWN, nullptr, &resp) && resp.ok;
}

} // namespace GpuCtlIpc
