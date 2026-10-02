#pragma once

#include "ipc/ram_oc_shm.h"
#include <string>

// Daemon lifecycle + transport for ram_oc_daemon (see dram_oc.h for why
// this GPL process-boundary split exists). Nothing here knows what DRAM telemetry looks like — it
// only knows "is a daemon alive, where, and how do I reach it." dram_oc.cpp
// is the only intended caller.

// vitals never starts, restarts or elevates ram_oc_daemon itself: the user
// starts it (Settings → Daemons shows the command). These only find and
// talk to one that's already running as this user or as root.

// Live control socket path of a trusted running daemon, or "".
std::string FindTrustedDaemon();

// Shared-memory telemetry frame, attached lazily once a daemon is running
// and dropped again when it goes away. Null if none is running.
const volatile VitalsTelemetryFrame* GetDaemonTelemetryFrame();

struct DaemonStatus { bool running = false, asRoot = false, ownedByUs = false; };
DaemonStatus GetRamOcDaemonStatus();

// Absolute path of the ram_oc_daemon binary next to this executable, or ""
// — for showing the user the exact start command.
std::string RamOcDaemonPath();

// Sends CMD_RUN_BENCHMARK to ram_oc_daemon over its control socket and
// blocks (on the caller's thread — call this from a background thread, same
// as BenchmarkController::RunMemory() already does) until the daemon's
// worker thread finishes bench_run() and replies. Returns false on any IPC
// failure, leaving `out` untouched.
bool RunDaemonBenchmark(VitalsBenchResultPOD& out);
