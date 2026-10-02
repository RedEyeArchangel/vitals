#pragma once
#include <string>

// Tiny sysfs read/write helpers shared by amd_backend.cpp. Kept separate
// from src/metrics/gpu_sysfs.cpp (which only ever reads) since this daemon
// is a different binary and needs the write half too.

std::string SysfsReadTrimmed(const std::string& path);
bool SysfsWriteString(const std::string& path, const std::string& content);
bool SysfsFileExists(const std::string& path);
