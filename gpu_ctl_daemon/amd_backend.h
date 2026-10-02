#pragma once
#include "ipc/gpu_ctl_shm.h"

#include <string>
#include <vector>

// AMD write backend: applies GpuCtlConfigPOD to the amdgpu sysfs ABI under
// /sys/class/drm/cardN/device{,/hwmon/hwmonM}. Same tree src/metrics/gpu_sysfs.cpp
// already reads from (read-only, in the main `vitals` binary) — this is the
// write half, root-only, living in gpu_ctl_daemon instead.

struct AmdGpuHandle {
    std::string devicePath;  // /sys/class/drm/cardN/device
    std::string hwmonPath;   // .../device/hwmon/hwmonM
    std::string pciSlot;     // e.g. "0000:0b:00.0", from devicePath's "uevent"/symlink
    std::string name;
};

// Scans /sys/class/drm for amdgpu (vendor 0x1002) cards with a hwmon child.
bool AmdDiscoverGpus(std::vector<AmdGpuHandle>& out);

// Fills driver-advertised bounds (power cap min/max, clock ranges from
// pp_od_clk_voltage's OD_RANGE section, pp_power_profile_mode names).
bool AmdGetCaps(const AmdGpuHandle& gpu, GpuCtlCapsPOD* out);

// Applies every field of cfg that's set (GPUCTL_UNSET/-1 fields are left
// alone), clamping against AmdGetCaps()'s bounds first and refusing
// (returning false + *err) rather than writing an out-of-range value.
// Starts/stops the software fan-curve poll thread as needed (see
// amd_backend.cpp's fan control section) — RDNA3+ PMFW options are applied
// directly to their own hwmon nodes instead, matching LACT's behavior of
// preferring PMFW hardware curve control when available.
bool AmdApplyConfig(const AmdGpuHandle& gpu, const GpuCtlConfigPOD& cfg, std::string* err);

// Resets performance level/clocks/fan to auto and stops any running
// fan-curve thread for this GPU. Used by GPUCTL_CMD_RESET_DEFAULT and by
// the revert path when reverting to "no config was ever confirmed".
void AmdResetToDefault(const AmdGpuHandle& gpu);

// Stops this GPU's fan-curve poll thread, if one is running, without
// touching any other setting — used before applying a new config so a new
// curve fully replaces the old one rather than racing with it.
void AmdStopFanCurveThread(const std::string& hwmonPath);

// Pure helpers, exposed for tools/gpuctl_check.cpp.
// Fills profileModeNames[idx]/profileModeCount from pp_power_profile_mode text.
void AmdParseProfileModes(const std::string& content, GpuCtlCapsPOD* out);
// Maps a fan config onto a fixed-size PMFW fan_curve (n points, clamped to
// the node's OD_RANGE): static = flat line, curve = user points or resampled.
void AmdPmfwCurvePoints(const GpuCtlFanConfigPOD& fan, int n, int tMin, int tMax, int sMin, int sMax, int outTemp[], int outPct[]);
