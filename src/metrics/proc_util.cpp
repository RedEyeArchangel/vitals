#include "metrics/proc_util.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <pwd.h>
#include <set>
#include <sstream>
#include <unistd.h>

std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string ReadTrimmed(const std::string& path) {
    std::string s = ReadFile(path);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

std::vector<std::string> ReadLines(const std::string& path) {
    std::vector<std::string> lines;
    std::istringstream ss(ReadFile(path));
    std::string line;
    while (std::getline(ss, line)) if (!line.empty()) lines.push_back(line);
    return lines;
}

std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string tok;
    while (ss >> tok) out.push_back(tok);
    return out;
}

std::string FormatKb(uint64_t kb) {
    char buf[32];
    if (kb >= 1024 * 1024) std::snprintf(buf, sizeof(buf), "%.2f GB", kb / (1024.0 * 1024.0));
    else std::snprintf(buf, sizeof(buf), "%.1f MB", kb / 1024.0);
    return buf;
}

std::string FormatUptime(double seconds) {
    long total = (long)seconds;
    long days = total / 86400;
    long hrs = (total % 86400) / 3600;
    long mins = (total % 3600) / 60;
    long secs = total % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld:%02ld", days, hrs, mins, secs);
    return buf;
}

std::string UsernameForPid(const std::string& base) {
    for (const std::string& line : ReadLines(base + "/status")) {
        if (line.rfind("Uid:", 0) == 0) {
            std::vector<std::string> parts = Split(line);
            if (parts.size() > 1) {
                uid_t uid = (uid_t)std::strtoul(parts[1].c_str(), nullptr, 10);
                struct passwd* pw = getpwuid(uid);
                if (pw && pw->pw_name) return pw->pw_name;
                return parts[1];
            }
        }
    }
    return "";
}

void ReadProcessDrmUsage(int pid, uint64_t& engineNs, uint64_t& vramKb) {
    std::string fdDir = "/proc/" + std::to_string(pid) + "/fd";
    DIR* d = opendir(fdDir.c_str());
    if (!d) return;
    // A process routinely holds several dup()'d fds referring to the same
    // underlying DRM open (compositors, Mesa clients, fork()'d children) —
    // each one reports the same cumulative counters, so counting every fd
    // multiplies busy-time and VRAM by however many dups exist. drm-client-id
    // identifies the underlying open across dups; skip repeats of one we've
    // already counted.
    std::set<uint64_t> seenClients;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        char target[64];
        ssize_t n = readlink((fdDir + "/" + ent->d_name).c_str(), target, sizeof(target) - 1);
        if (n <= 0) continue;
        target[n] = '\0';
        if (std::strncmp(target, "/dev/dri/", 9) != 0) continue;

        std::vector<std::string> lines = ReadLines("/proc/" + std::to_string(pid) + "/fdinfo/" + ent->d_name);
        bool dup = false;
        for (const std::string& line : lines) {
            if (line.rfind("drm-client-id:", 0) == 0) {
                std::vector<std::string> val = Split(line.substr(line.find(':') + 1));
                if (!val.empty() && !seenClients.insert(std::strtoull(val[0].c_str(), nullptr, 10)).second) dup = true;
                break;
            }
        }
        if (dup) continue;

        for (const std::string& line : lines) {
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::vector<std::string> val = Split(line.substr(colon + 1));
            if (val.empty()) continue;
            // Only the 3D/compute engine ("gfx" on amdgpu, "render" on
            // i915/xe) — not video decode/encode/sdma/copy. The system-wide
            // GPU% (gpu_busy_percent on amdgpu) tracks only that same engine,
            // so summing every engine here made a video-decode-heavy process
            // read far higher than the whole-GPU number it's compared against.
            std::string engine = line.rfind("drm-engine-", 0) == 0 ? line.substr(11, colon - 11) : "";
            if (engine == "gfx" || engine == "render") {
                engineNs += std::strtoull(val[0].c_str(), nullptr, 10);
            } else if (line.rfind("drm-memory-vram:", 0) == 0) {
                vramKb += std::strtoull(val[0].c_str(), nullptr, 10);
            }
        }
    }
    closedir(d);
}

const char* StatusForChar(char c) {
    switch (c) {
        case 'R': return "running";
        case 'S': return "sleeping";
        case 'D': return "disk sleep";
        case 'Z': return "zombie";
        case 'T': return "stopped";
        case 't': return "tracing stop";
        default:  return "unknown";
    }
}
