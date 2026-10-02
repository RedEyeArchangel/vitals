#pragma once

#include <atomic>

// Shared between BenchmarkController (writes it, via mprime's redirected
// stdout/stderr) and the Benchmarks page (reads it directly, throttled, to
// live-tail the test while it runs) — one literal so the two can't drift.
inline const char* kRamTestDir = "oc/p95v3019b20.linux64";
inline const char* kRamTestLogPath = "oc/p95v3019b20.linux64/vitals_ram_test.log";

struct MemoryBenchResult {
    double latL1Ns = 0.0, latL2Ns = 0.0, latL3Ns = 0.0, latDramNs = 0.0;
    double bwReadMBs = 0.0, bwWriteMBs = 0.0, bwCopyMBs = 0.0;
    bool valid = false;
};

// Raw floating-point throughput across all logical cores, ~1.5s fixed
// window — a self-consistent relative score (not comparable to Cinebench
// or similar), useful for before/after comparisons on the same machine.
struct CpuBenchResult {
    double scoreMops = 0.0; // million ops/sec, all threads combined
    bool valid = false;
};

// Result of one mprime (Prime95) torture-test run — a stability check, not a
// scored benchmark: errors == 0 means no hardware faults were caught, not
// that the machine is fast.
struct RamTestResult {
    int errors = 0;
    bool valid = false;
    bool toolMissing = false; // oc/p95v3019b20.linux64/mprime not found
};

// Off-the-UI-thread benchmark/stress orchestration for the Benchmarks page.
// Owned by Metrics (see Metrics::bench) but kept as its own type: Metrics is
// "what the machine is doing right now" (refreshed once a frame by
// Update()), this is "what we told it to do" (side-effecting — spawns
// threads/subprocesses, runs for a caller-given duration). Callers pass in
// the Metrics fields each Run* needs (nthreads, memTotalGB, current t)
// rather than this type reaching back into Metrics itself.
//
// Each *Running flag gates reads of its result the same way: the worker
// writes the result struct, then releases *Running=false (release); callers
// must only read the result after observing *Running==false (acquire) — no
// mutex. GPU stress is different: it's frame-by-frame overdraw on the main
// GL thread (see gpuStressUntilT), not a worker thread, so it has no
// Running flag — main.cpp's render loop just compares gpuStressUntilT
// against the current t every frame.
struct BenchmarkController {
    MemoryBenchResult memResult; // ram_oc/bench.c's memory latency/bandwidth test, over the daemon socket
    std::atomic<bool> memRunning{false};
    void RunMemory(); // no-op if already running

    CpuBenchResult cpuResult;
    std::atomic<bool> cpuRunning{false};
    void RunCpu(int nthreads); // no-op if already running

    // Configurable CPU load generator: `loadPct` of every ~20ms duty cycle,
    // on `nthreads` cores, for `durationSec`. Watch the CPU/Thermals pages'
    // live telemetry while it runs instead of a bespoke reporting path.
    std::atomic<bool> stressRunning{false};
    std::atomic<bool> stressStopRequested{false};
    std::atomic<float> stressRemainingSec{0.0f};
    void RunStress(float durationSec, int loadPct, int nthreads); // no-op if already running
    void StopStress() { stressStopRequested.store(true, std::memory_order_relaxed); }

    // RAM stability test: runs the bundled mprime (Prime95) torture test as a
    // subprocess for durationSec, configured (via memTotalGB) to exercise a
    // large chunk of RAM rather than just cache — see RunRamTest in
    // benchmark_controller.cpp. Errors == "hardware fault detected", not a
    // performance score.
    RamTestResult ramResult;
    std::atomic<bool> ramRunning{false};
    std::atomic<bool> ramStopRequested{false};
    std::atomic<float> ramRemainingSec{0.0f};
    // Live "FATAL ERROR" count while the test is still running — the page
    // polls this for a live badge; ramResult.errors is the same count
    // frozen at whatever it was when the run finished.
    std::atomic<int> ramLiveErrors{0};
    void RunRamTest(float durationSec, float memTotalGB); // no-op if already running
    void StopRamTest() { ramStopRequested.store(true, std::memory_order_relaxed); }

    // GPU load: main.cpp's render loop checks this deadline every frame and,
    // while active, draws extra overdraw (scaled by gpuStressLoadPct) to load
    // the GPU — no compute-shader plumbing needed since ImGui's own draw
    // calls already exercise the GPU. Watch the GPU panel's live gpuPct/temp
    // while it runs.
    double gpuStressUntilT = -1.0; // t deadline; gpuStressUntilT <= t means inactive
    int gpuStressLoadPct = 100;
    void RunGpuStress(double now, float durationSec, int loadPct) { gpuStressLoadPct = loadPct; gpuStressUntilT = now + durationSec; }
    void StopGpuStress(double now) { gpuStressUntilT = now; }
};
