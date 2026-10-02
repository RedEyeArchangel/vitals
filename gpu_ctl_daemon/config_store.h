#pragma once
#include "ipc/gpu_ctl_shm.h"

#include <string>
#include <unordered_map>

// Persists only CONFIRMED config (see pending_apply.h) to root-owned
// /etc/vitals/gpud.conf, applied on daemon startup. Deliberately not
// ~/.config/vitals/settings.conf (src/settings_persistence.cpp): that file
// is user-writable, and a root daemon trusting a user-writable file on boot
// would let any local user plant a config that gets reapplied as root next
// boot. Format: one `<pciSlot>=<base64 of the raw GpuCtlConfigPOD bytes>`
// line per GPU — a hand-written field-by-field key=value schema (like
// settings_persistence.cpp's) would work too, but this file is never meant
// to be hand-edited (unlike LACT's YAML), so a raw POD dump avoids ~200
// lines of mechanical (de)serialization for every nested fan/pmfw/clocks
// field. A size check against sizeof(GpuCtlConfigPOD) before trusting a
// decoded entry gives the same "ignore stale/foreign data" safety
// settings_persistence.cpp gets from ignoring unknown keys.
namespace ConfigStore {

// Reads every persisted <pciSlot, config> pair from /etc/vitals/gpud.conf.
// Missing file / unreadable / corrupt entries are simply absent from the
// result — never an error, same as SettingsPersistence::LoadSettings().
std::unordered_map<std::string, GpuCtlConfigPOD> LoadAll();

// Rewrites the whole file with `all`. Best-effort; failures are logged to
// syslog (via main.cpp's LogWarn) and otherwise ignored, same posture as
// SettingsPersistence::SaveSettings().
void SaveAll(const std::unordered_map<std::string, GpuCtlConfigPOD>& all);

} // namespace ConfigStore
