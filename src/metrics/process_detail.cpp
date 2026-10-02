#include "metrics/process_detail.h"
#include "metrics/proc_util.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

static std::string FormatCpuTime(uint64_t totalTicks, long clkTck) {
    uint64_t centiseconds = clkTck > 0 ? (totalTicks * 100) / (uint64_t)clkTck : 0;
    uint64_t cs = centiseconds % 100;
    uint64_t totalSeconds = centiseconds / 100;
    uint64_t h = totalSeconds / 3600, mnt = (totalSeconds % 3600) / 60, s = totalSeconds % 60;
    char buf[32];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%llu:%02llu:%02llu", (unsigned long long)h, (unsigned long long)mnt, (unsigned long long)s);
    else std::snprintf(buf, sizeof(buf), "%llu:%02llu.%02llu", (unsigned long long)mnt, (unsigned long long)s, (unsigned long long)cs);
    return buf;
}

static std::string FormatStarted(time_t startEpoch) {
    time_t now = time(nullptr);
    struct tm nowTm, startTm;
    localtime_r(&now, &nowTm);
    localtime_r(&startEpoch, &startTm);
    char buf[64];
    if (nowTm.tm_year == startTm.tm_year && nowTm.tm_yday == startTm.tm_yday)
        std::strftime(buf, sizeof(buf), "Today %H:%M", &startTm);
    else
        std::strftime(buf, sizeof(buf), "%b %d %H:%M", &startTm);
    return buf;
}

static const char* PriorityLabel(int nice) {
    if (nice <= -11) return "Very High";
    if (nice <= -1) return "High";
    if (nice == 0) return "Normal";
    if (nice <= 10) return "Low";
    return "Very Low";
}

// On-demand only (see ProcessDetail in metrics.h) — never called from the
// per-tick Metrics::Update() loop.
ProcessDetail ReadProcessDetail(int pid, float liveCpuPct) {
    ProcessDetail out;
    out.pid = pid;
    std::string base = "/proc/" + std::to_string(pid);

    std::string name = ReadFile(base + "/comm");
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
    if (name.empty()) return out; // process no longer exists
    out.found = true;
    out.name = name;
    out.user = UsernameForPid(base);
    out.cpuPct = liveCpuPct;

    uint64_t vmSizeKb = 0, vmRssKb = 0, rssShmemKb = 0;
    for (const std::string& line : ReadLines(base + "/status")) {
        std::vector<std::string> p = Split(line);
        if (p.size() < 2) continue;
        if (line.rfind("VmSize:", 0) == 0) vmSizeKb = std::strtoull(p[1].c_str(), nullptr, 10);
        else if (line.rfind("VmRSS:", 0) == 0) vmRssKb = std::strtoull(p[1].c_str(), nullptr, 10);
        else if (line.rfind("RssShmem:", 0) == 0) rssShmemKb = std::strtoull(p[1].c_str(), nullptr, 10);
    }
    out.virtualText = FormatKb(vmSizeKb);
    out.residentText = FormatKb(vmRssKb);
    out.sharedText = FormatKb(rssShmemKb);

    // smaps_rollup needs the process to still exist and (for other users'
    // processes) elevated permissions — fall back to RSS/"N/A" if unreadable.
    uint64_t pssKb = 0;
    long writableKb = -1;
    bool havePss = false;
    for (const std::string& line : ReadLines(base + "/smaps_rollup")) {
        std::vector<std::string> p = Split(line);
        if (p.size() < 2) continue;
        if (line.rfind("Pss:", 0) == 0) { pssKb = std::strtoull(p[1].c_str(), nullptr, 10); havePss = true; }
        else if (line.rfind("Private_Clean:", 0) == 0 || line.rfind("Private_Dirty:", 0) == 0)
            writableKb = std::max(0L, writableKb) + std::strtol(p[1].c_str(), nullptr, 10);
    }
    out.memoryText = havePss ? FormatKb(pssKb) : out.residentText;
    out.writableText = writableKb >= 0 ? FormatKb((uint64_t)writableKb) : "N/A";

    long clkTck = sysconf(_SC_CLK_TCK);
    std::string stat = ReadFile(base + "/stat");
    size_t close = stat.rfind(')');
    char state = '?';
    if (close != std::string::npos && close + 2 < stat.size()) {
        state = stat[close + 2];
        // 0-indexed after pid/comm/state — see the matching comment in
        // Metrics::Update()'s process-list loop for the full field layout.
        std::vector<std::string> rest = Split(stat.substr(close + 2));
        if (rest.size() > 19) {
            uint64_t utime = std::strtoull(rest[11].c_str(), nullptr, 10);
            uint64_t stime = std::strtoull(rest[12].c_str(), nullptr, 10);
            out.cpuTimeText = FormatCpuTime(utime + stime, clkTck);
            out.nice = std::atoi(rest[16].c_str());
            out.priorityText = PriorityLabel(out.nice);

            uint64_t starttimeTicks = std::strtoull(rest[19].c_str(), nullptr, 10);
            double btime = 0;
            for (const std::string& l : ReadLines("/proc/stat")) {
                if (l.rfind("btime ", 0) == 0) { btime = std::strtod(l.c_str() + 6, nullptr); break; }
            }
            if (btime > 0 && clkTck > 0)
                out.startedText = FormatStarted((time_t)(btime + (double)starttimeTicks / (double)clkTck));
        }
    }
    out.status = StatusForChar(state);

    out.commandLine = ReadFile(base + "/cmdline");
    for (char& c : out.commandLine) if (c == '\0') c = ' ';
    while (!out.commandLine.empty() && out.commandLine.back() == ' ') out.commandLine.pop_back();
    if (out.commandLine.empty()) out.commandLine = name; // kernel threads: cmdline is empty

    out.waitingChannel = ReadTrimmed(base + "/wchan");
    if (out.waitingChannel.empty()) out.waitingChannel = "0";

    // SELinux only — AppArmor doesn't expose a per-process confinement label
    // this cheaply, so this reads "Unavailable" rather than guess "unconfined".
    out.securityContext = ReadTrimmed(base + "/attr/current");
    if (out.securityContext.empty()) out.securityContext = "Unavailable";

    for (const std::string& line : ReadLines(base + "/cgroup")) {
        size_t lastColon = line.rfind(':');
        if (lastColon != std::string::npos) { out.controlGroup = line.substr(lastColon + 1); break; }
    }
    if (out.controlGroup.empty()) out.controlGroup = "Unavailable";

    return out;
}
