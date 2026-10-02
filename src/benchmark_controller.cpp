#include "benchmark_controller.h"
#include "dram_oc/dram_oc.h"
#include "metrics/proc_util.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <future>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

void BenchmarkController::RunMemory() {
    bool expected = false;
    if (!memRunning.compare_exchange_strong(expected, true)) return; // already running

    std::thread([this]() {
        MemoryBenchResult r;
        // Blocks ~2-4s (the daemon's worker thread runs bench_run() and
        // replies over the socket when done) — hence the detached thread.
        RemoteBenchResult remote;
        if (RunRemoteBenchmark(remote)) {
            r.latL1Ns = remote.latL1Ns;
            r.latL2Ns = remote.latL2Ns;
            r.latL3Ns = remote.latL3Ns;
            r.latDramNs = remote.latDramNs;
            r.bwReadMBs = remote.bwReadMBs;
            r.bwWriteMBs = remote.bwWriteMBs;
            r.bwCopyMBs = remote.bwCopyMBs;
            r.valid = true;
        }
        memResult = r;
        memRunning.store(false, std::memory_order_release); // publishes memResult to the UI thread
    }).detach();
}

// Burns floating-point ops on one thread for `deadline`, returning how many
// it completed. `volatile` keeps the compiler from hoisting the loop away.
static uint64_t CpuBenchWorker(std::chrono::steady_clock::time_point deadline) {
    uint64_t ops = 0;
    volatile double x = 1.0000001;
    while (std::chrono::steady_clock::now() < deadline) {
        for (int i = 0; i < 100000; ++i) {
            x = std::sqrt(x * x + 1.0000001);
            ++ops;
        }
    }
    return ops;
}

void BenchmarkController::RunCpu(int nthreads) {
    bool expected = false;
    if (!cpuRunning.compare_exchange_strong(expected, true)) return; // already running

    nthreads = std::max(1, nthreads);
    std::thread([this, nthreads]() {
        const auto duration = std::chrono::milliseconds(1500);
        auto deadline = std::chrono::steady_clock::now() + duration;

        std::vector<std::future<uint64_t>> futures;
        for (int i = 0; i < nthreads; ++i)
            futures.push_back(std::async(std::launch::async, CpuBenchWorker, deadline));

        uint64_t totalOps = 0;
        for (auto& f : futures) totalOps += f.get();

        cpuResult.scoreMops = (double)totalOps / 1e6 / std::chrono::duration<double>(duration).count();
        cpuResult.valid = true;
        cpuRunning.store(false, std::memory_order_release); // publishes cpuResult to the UI thread
    }).detach();
}

void BenchmarkController::RunStress(float durationSec, int loadPct, int nthreads) {
    bool expected = false;
    if (!stressRunning.compare_exchange_strong(expected, true)) return; // already running
    stressStopRequested.store(false, std::memory_order_relaxed);

    loadPct = std::clamp(loadPct, 1, 100);
    nthreads = std::max(1, nthreads);
    stressRemainingSec.store(durationSec, std::memory_order_relaxed);

    std::thread([this, durationSec, loadPct, nthreads]() {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(durationSec);
        auto notStopped = [this] { return !stressStopRequested.load(std::memory_order_relaxed); };

        std::vector<std::thread> workers;
        for (int i = 0; i < nthreads; ++i) {
            workers.emplace_back([this, deadline, loadPct, notStopped]() {
                const auto period = std::chrono::milliseconds(20);
                const auto busyFor = period * loadPct / 100;
                while (std::chrono::steady_clock::now() < deadline && notStopped()) {
                    auto cycleStart = std::chrono::steady_clock::now();
                    while (std::chrono::steady_clock::now() - cycleStart < busyFor) {
                        volatile double x = 1.0000001;
                        x = x * x;
                    }
                    auto elapsed = std::chrono::steady_clock::now() - cycleStart;
                    if (elapsed < period) std::this_thread::sleep_for(period - elapsed);
                }
            });
        }

        while (std::chrono::steady_clock::now() < deadline && notStopped()) {
            float remain = std::chrono::duration<float>(deadline - std::chrono::steady_clock::now()).count();
            stressRemainingSec.store(std::max(0.0f, remain), std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        for (std::thread& w : workers) w.join();

        stressRemainingSec.store(0.0f, std::memory_order_relaxed);
        stressRunning.store(false, std::memory_order_release);
    }).detach();
}

// mprime keeps its own prime.txt (config) and appends/preserves keys it
// doesn't recognize, so seeding these once is enough — it won't get
// clobbered on later runs. Both keys are documented in oc/p95v3019b20.linux64/
// undoc.txt: TortureMem sizes the torture test's memory footprint (MB), and
// turning off TortureAlternateInPlace "may do a better job of stressing all RAM"
// (its default alternates with a small in-place/cache-only pass).
static void SeedRamTortureConfig(const std::string& dir, float memTotalGB) {
    std::string path = dir + "/prime.txt";
    std::string existing = ReadFile(path);
    bool haveMem = existing.find("TortureMem=") != std::string::npos;
    bool haveAlt = existing.find("TortureAlternateInPlace=") != std::string::npos;
    if (haveMem && haveAlt) return;

    std::ofstream f(path, std::ios::app);
    if (!f) return;
    if (!haveMem) f << "TortureMem=" << std::max(256, (int)(memTotalGB * 1024.0f * 0.8f)) << "\n";
    if (!haveAlt) f << "TortureAlternateInPlace=0\n";
}

void BenchmarkController::RunRamTest(float durationSec, float memTotalGB) {
    bool expected = false;
    if (!ramRunning.compare_exchange_strong(expected, true)) return; // already running
    ramStopRequested.store(false, std::memory_order_relaxed);
    ramRemainingSec.store(durationSec, std::memory_order_relaxed);
    ramLiveErrors.store(0, std::memory_order_relaxed);

    std::thread([this, durationSec, memTotalGB]() {
        RamTestResult r;
        auto finish = [&] {
            ramResult = r; // publish before clearing running (release, below)
            ramRemainingSec.store(0.0f, std::memory_order_relaxed);
            ramRunning.store(false, std::memory_order_release);
        };

        // Bundled Prime95 build (see oc/p95v3019b20.linux64/readme.txt), run
        // relative to cwd like assets/fonts/ elsewhere in this codebase —
        // vitals is always launched from the repo root (see README).
        const std::string dir = kRamTestDir;
        const std::string exe = dir + "/mprime";
        if (access(exe.c_str(), F_OK) != 0) { r.toolMissing = true; r.valid = true; finish(); return; }
        chmod(exe.c_str(), 0755); // the vendored binary ships without +x

        SeedRamTortureConfig(dir, memTotalGB);

        const std::string logPath = kRamTestLogPath;
        pid_t pid = fork();
        if (pid == 0) {
            // Dies with us — a forgotten/crashed vitals must never leave an
            // all-cores, most-of-RAM torture test running unattended.
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (chdir(dir.c_str()) != 0) _exit(127);
            int logFd = open("vitals_ram_test.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (logFd >= 0) { dup2(logFd, STDOUT_FILENO); dup2(logFd, STDERR_FILENO); close(logFd); }
            // mprime fully-buffers stdout when it's not a tty, so without
            // forcing line buffering the Benchmarks page's live log tail
            // would stay empty until the process exits. stdbuf -oL fixes
            // that; if stdbuf isn't installed, fall back to running mprime
            // directly (the test itself is unaffected either way — only
            // the live view's responsiveness is).
            execlp("stdbuf", "stdbuf", "-oL", "./mprime", "-t", (char*)nullptr);
            execl("mprime", "mprime", "-t", (char*)nullptr);
            _exit(127);
        }
        if (pid < 0) { r.valid = true; finish(); return; }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(durationSec);
        bool exitedEarly = false;
        while (std::chrono::steady_clock::now() < deadline &&
               !ramStopRequested.load(std::memory_order_relaxed)) {
            int status = 0;
            if (waitpid(pid, &status, WNOHANG) == pid) { exitedEarly = true; break; }
            float remain = std::chrono::duration<float>(deadline - std::chrono::steady_clock::now()).count();
            ramRemainingSec.store(std::max(0.0f, remain), std::memory_order_relaxed);

            std::string log = ReadFile(logPath);
            int liveErrors = 0;
            for (size_t pos = log.find("FATAL ERROR"); pos != std::string::npos; pos = log.find("FATAL ERROR", pos + 1))
                ++liveErrors;
            ramLiveErrors.store(liveErrors, std::memory_order_relaxed);

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        if (!exitedEarly) {
            // "Hit ^C to end this test" — stress.txt's own documented way to stop it.
            kill(pid, SIGINT);
            int status = 0;
            bool exited = false;
            for (int i = 0; i < 50 && !exited; ++i) {
                if (waitpid(pid, &status, WNOHANG) == pid) exited = true;
                else std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!exited) { kill(pid, SIGKILL); waitpid(pid, &status, 0); }
        }

        std::string log = ReadFile(logPath);
        int errors = 0;
        for (size_t pos = log.find("FATAL ERROR"); pos != std::string::npos; pos = log.find("FATAL ERROR", pos + 1))
            ++errors;
        r.errors = errors;
        r.valid = true;
        finish();
    }).detach();
}
