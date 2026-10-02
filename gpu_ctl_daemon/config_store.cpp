#include "config_store.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace {

const char* kConfigPath = "/etc/vitals/gpud.conf";
const char kB64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const void* data, size_t len) {
    const auto* bytes = (const unsigned char*)data;
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        uint32_t n = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
        out += kB64Chars[(n >> 18) & 0x3F]; out += kB64Chars[(n >> 12) & 0x3F];
        out += kB64Chars[(n >> 6) & 0x3F]; out += kB64Chars[n & 0x3F];
    }
    size_t rem = len - i;
    if (rem == 1) {
        uint32_t n = bytes[i] << 16;
        out += kB64Chars[(n >> 18) & 0x3F]; out += kB64Chars[(n >> 12) & 0x3F]; out += "==";
    } else if (rem == 2) {
        uint32_t n = (bytes[i] << 16) | (bytes[i + 1] << 8);
        out += kB64Chars[(n >> 18) & 0x3F]; out += kB64Chars[(n >> 12) & 0x3F]; out += kB64Chars[(n >> 6) & 0x3F]; out += "=";
    }
    return out;
}

int B64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool Base64Decode(const std::string& in, std::string* out) {
    out->clear();
    out->reserve(in.size() / 4 * 3);
    int vals[4]; int n = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r') break;
        int v = B64Val(c);
        if (v < 0) return false;
        vals[n++] = v;
        if (n == 4) {
            uint32_t x = (vals[0] << 18) | (vals[1] << 12) | (vals[2] << 6) | vals[3];
            out->push_back((char)((x >> 16) & 0xFF));
            out->push_back((char)((x >> 8) & 0xFF));
            out->push_back((char)(x & 0xFF));
            n = 0;
        }
    }
    if (n == 2) {
        uint32_t x = (vals[0] << 18) | (vals[1] << 12);
        out->push_back((char)((x >> 16) & 0xFF));
    } else if (n == 3) {
        uint32_t x = (vals[0] << 18) | (vals[1] << 12) | (vals[2] << 6);
        out->push_back((char)((x >> 16) & 0xFF));
        out->push_back((char)((x >> 8) & 0xFF));
    } else if (n != 0) {
        return false;
    }
    return true;
}

} // namespace

namespace ConfigStore {

std::unordered_map<std::string, GpuCtlConfigPOD> LoadAll() {
    std::unordered_map<std::string, GpuCtlConfigPOD> result;
    std::ifstream f(kConfigPath);
    if (!f) return result;

    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string pciSlot = line.substr(0, eq);
        std::string decoded;
        if (!Base64Decode(line.substr(eq + 1), &decoded)) continue;
        if (decoded.size() != sizeof(GpuCtlConfigPOD)) continue; // stale/foreign entry — ignore, don't guess
        GpuCtlConfigPOD cfg;
        std::memcpy(&cfg, decoded.data(), sizeof(cfg));
        gpuctl_config_sanitize(&cfg);
        result[pciSlot] = cfg;
    }
    return result;
}

void SaveAll(const std::unordered_map<std::string, GpuCtlConfigPOD>& all) {
    mkdir("/etc/vitals", 0755); // best-effort; EEXIST fine to ignore here
    // Write-then-rename so a crash/power loss mid-write never leaves a
    // truncated file behind (which LoadAll would silently treat as "no config").
    std::string tmp = std::string(kConfigPath) + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return;
        f << "# vitals gpu_ctl_daemon: confirmed GPU config, applied on daemon startup.\n"
          << "# Not meant to be hand-edited — each line is a base64 dump of GpuCtlConfigPOD.\n";
        for (const auto& [pciSlot, cfg] : all) {
            f << pciSlot << "=" << Base64Encode(&cfg, sizeof(cfg)) << "\n";
        }
        if (!f.flush()) { std::remove(tmp.c_str()); return; }
    }
    chmod(tmp.c_str(), 0600); // root-only: a hostile config here is a privesc vector (see header comment)
    if (std::rename(tmp.c_str(), kConfigPath) != 0) std::remove(tmp.c_str());
}

} // namespace ConfigStore
