#include "metrics/services.h"
#include "metrics/proc_util.h"

#include <algorithm>
#include <cstdio>

std::vector<ServiceUnit> ReadServices() {
    std::vector<ServiceUnit> out;
    FILE* p = popen("systemctl list-units --type=service --all --no-legend --no-pager --plain 2>/dev/null", "r");
    if (!p) return out;
    char buf[512];
    while (fgets(buf, sizeof(buf), p)) {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        std::vector<std::string> f = Split(line);
        if (f.size() < 4) continue;
        ServiceUnit s;
        s.name = f[0];
        s.load = f[1];
        s.active = f[2];
        s.sub = f[3];
        for (size_t i = 4; i < f.size(); ++i) { if (i > 4) s.description += ' '; s.description += f[i]; }
        out.push_back(s);
    }
    pclose(p);
    std::sort(out.begin(), out.end(), [](const ServiceUnit& a, const ServiceUnit& b) { return a.name < b.name; });
    return out;
}
