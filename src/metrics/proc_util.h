#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Generic /proc- and /sys-reading primitives shared by every metrics/*.cpp
// backend reader (and by Metrics::Update() itself). No app-specific types —
// see the sibling headers (thermal_sensors.h, gpu_sysfs.h, ...) for those.

std::string ReadFile(const std::string& path);

// ReadFile with trailing whitespace/newlines stripped — for the common case
// of a sysfs "single value per file" node.
std::string ReadTrimmed(const std::string& path);

std::vector<std::string> ReadLines(const std::string& path);

std::vector<std::string> Split(const std::string& s);

std::string FormatKb(uint64_t kb);

std::string FormatUptime(double seconds);

// Reads "Uid:" from /proc/<pid>/status and resolves it to a username via
// getpwuid(); falls back to the raw uid string if the account isn't found.
std::string UsernameForPid(const std::string& base);

// Maps a /proc/<pid>/stat state char ('R', 'S', 'D', ...) to a display label.
const char* StatusForChar(char c);

// Sums DRM engine busy-time (ns, cumulative) and VRAM (KB, current) across
// every /dev/dri fd the process holds open, via /proc/<pid>/fdinfo/<fd>.
// Works for any driver implementing the kernel's DRM client usage-stats
// interface (amdgpu, i915, xe, ...); leaves both at 0 for GPUs that don't
// (e.g. older proprietary nvidia). Caller diffs engineNs against a previous
// sample over elapsed time to get a percent, same as /proc/<pid>/stat jiffies.
void ReadProcessDrmUsage(int pid, uint64_t& engineNs, uint64_t& vramKb);
