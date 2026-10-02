#pragma once

#include "metrics.h" // DiskVolume

#include <vector>

// Real mounted volumes only — /proc/mounts lines whose device starts with
// "/dev/" (skips the pseudo-filesystems: proc, sysfs, tmpfs, cgroup, ...)
// and snap/flatpak loop-squashfs mounts. Backs the Disk Space page.
std::vector<DiskVolume> ReadDiskVolumes();
