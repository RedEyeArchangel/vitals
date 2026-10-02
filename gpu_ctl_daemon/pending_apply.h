#pragma once
#include "ipc/gpu_ctl_shm.h"

#include <chrono>
#include <functional>
#include <string>

// Confirm/auto-revert state machine (see plan §7 "Safety"): SET_CONFIG
// applies immediately and arms a timer; if CONFIRM doesn't arrive before it
// expires, the config reverts. LACT has the same mechanic
// (`apply_settings_timer`, default 5s, see LACT-master/docs/CONFIG.md) to
// protect against a clock offset that makes the system unusable before the
// user can undo it. Lives server-side (not in `vitals`) so it survives the
// GUI crashing or being closed mid-timer.
namespace PendingApply {

// Call once from main.cpp's poll loop (e.g. every ~200ms). Any GPU whose
// timer has expired gets `revertFn(pciSlot, hasConfirmed, lastConfirmed)`
// invoked — hasConfirmed=false means "nothing was ever confirmed for this
// GPU", so the caller should reset to hardware defaults instead of
// reapplying a config.
using RevertFn = std::function<void(const std::string& pciSlot, bool hasConfirmed, const GpuCtlConfigPOD& lastConfirmed)>;
void CheckExpirations(const RevertFn& revertFn);

// Arms/re-arms the revert timer for `pciSlot`, `timeoutSec` from now, and
// remembers `applied` as the config awaiting confirmation.
void Arm(const std::string& pciSlot, uint32_t timeoutSec, const GpuCtlConfigPOD& applied);

// If a config is pending for `pciSlot`, cancels the timer, makes it the new
// last-confirmed config, copies it to *out and returns true. Returns false
// when nothing is pending (never armed, or the timer already reverted it) —
// the caller must not persist anything in that case.
bool Confirm(const std::string& pciSlot, GpuCtlConfigPOD* out);

// Cancels any pending timer without confirming (manual revert).
void Cancel(const std::string& pciSlot);

// Drops pending + confirmed state entirely (reset to hardware defaults).
void Forget(const std::string& pciSlot);

// Seeds the last-confirmed config at daemon startup (from ConfigStore::LoadAll)
// without arming a timer — these were already confirmed in a prior run.
void SeedConfirmed(const std::string& pciSlot, const GpuCtlConfigPOD& cfg);

bool IsPending(const std::string& pciSlot);

// For GPUCTL_CMD_GET_CONFIG: the last-confirmed config, if any.
bool GetConfirmed(const std::string& pciSlot, GpuCtlConfigPOD* out);

} // namespace PendingApply
