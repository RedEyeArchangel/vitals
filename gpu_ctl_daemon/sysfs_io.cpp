#include "sysfs_io.h"

#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

std::string SysfsReadTrimmed(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return "";
    // sysfs files are at most one page; pp_od_clk_voltage (with OD_VDDC_CURVE)
    // and RDNA3's pp_power_profile_mode table both exceed a small buffer.
    std::string s(4096, '\0');
    size_t n = std::fread(&s[0], 1, s.size(), f);
    std::fclose(f);
    s.resize(n);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

// One write() per call, matching pp_od_clk_voltage's per-command write
// protocol (each command line must land in its own write() syscall — the
// driver parses one command per write, not per line of a larger buffer).
bool SysfsWriteString(const std::string& path, const std::string& content) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    size_t n = std::fwrite(content.data(), 1, content.size(), f);
    int closeRc = std::fclose(f);
    return n == content.size() && closeRc == 0;
}

bool SysfsFileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}
