#include "metrics/disk_volumes.h"
#include "metrics/proc_util.h"

#include <algorithm>
#include <set>
#include <sys/statvfs.h>

std::vector<DiskVolume> ReadDiskVolumes() {
    std::vector<DiskVolume> out;
    std::set<std::string> seenMounts;
    for (const std::string& line : ReadLines("/proc/mounts")) {
        std::vector<std::string> f = Split(line);
        if (f.size() < 3 || f[0].rfind("/dev/", 0) != 0) continue;
        if (f[0].rfind("/dev/loop", 0) == 0 || f[2] == "squashfs") continue; // snap/flatpak backing mounts, not real volumes
        if (!seenMounts.insert(f[1]).second) continue; // skip bind-mount duplicates

        struct statvfs sv;
        if (statvfs(f[1].c_str(), &sv) != 0 || sv.f_blocks == 0) continue;

        DiskVolume v;
        v.device = f[0];
        v.mountPoint = f[1];
        v.fsType = f[2];
        double blockGB = (double)sv.f_frsize / (1024.0 * 1024.0 * 1024.0);
        v.totalGB = (double)sv.f_blocks * blockGB;
        v.freeGB = (double)sv.f_bfree * blockGB;
        v.usedGB = v.totalGB - v.freeGB;
        v.usedPct = v.totalGB > 0.0 ? (float)(v.usedGB / v.totalGB * 100.0) : 0.0f;
        out.push_back(v);
    }
    std::sort(out.begin(), out.end(), [](const DiskVolume& a, const DiskVolume& b) { return a.mountPoint < b.mountPoint; });
    return out;
}
