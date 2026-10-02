#include "pages/benchmarks/benchmarks.h"
#include "pages/performance/shared.h" // BeginAltCardStyle/EndAltCardStyle — nested "card on card" for the live log panel
#include "metrics/proc_util.h"        // ReadFile — live-tailing the RAM test's log directly, see DrawLiveLog
#include "theme.h"
#include "widgets.h"

#include <algorithm>
#include <cstdio>

namespace {

// Small accent-colored dot + bold title, used at the top of every card here
// instead of a plain TextColored — one place to add an identity color per
// benchmark type (matches the metric-accent convention everywhere else).
void SectionTitle(const char* label, ImVec4 accent) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = Theme::Scale(4.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f), r, ImGui::ColorConvertFloat4ToU32(accent));
    ImGui::Dummy(ImVec2(r * 2.0f + Theme::Scale(8.0f), 0));
    ImGui::SameLine(0, 0);
    ImGui::PushFont(Theme::LoadedFonts.bodyBold);
    ImGui::TextColored(Theme::kTextPrimary, "%s", label);
    ImGui::PopFont();
}

void Row(const char* label, const char* value, float width) {
    ImGui::TextColored(Theme::kTextMuted, "%s", label);
    ImGui::SameLine(width - ImGui::CalcTextSize(value).x - 8.0f);
    ImGui::PushFont(Theme::LoadedFonts.mono);
    ImGui::TextColored(Theme::kTextPrimary, "%s", value);
    ImGui::PopFont();
}

// Elapsed/duration progress for a running timed test — reuses the app's
// signature LED-bar meter instead of a plain ImGui::ProgressBar so it reads
// consistently with every other "how full" meter in the app.
void ElapsedBar(float fraction, ImVec4 color, float width) {
    Widgets::LedBarHorizontal("progress", std::clamp(fraction, 0.0f, 1.0f), color, ImVec2(width, Theme::Scale(10.0f)), 40);
}

// Live-tails oc/p95v3019b20.linux64/vitals_ram_test.log directly (no
// controller-side buffering needed — a plain re-read is cheap for a log
// this size, and reading a file another process is still appending to is
// safe on Linux, worst case a torn last line that fills in next read).
// Throttled to 2x/sec so it isn't reopening the file every single frame.
void DrawLiveLog(float width) {
    static std::string tail;
    static double lastRead = -1.0;
    double now = ImGui::GetTime();
    if (now - lastRead > 0.5) {
        lastRead = now;
        std::string log = ReadFile(kRamTestLogPath);
        const size_t kMaxTail = 4000;
        tail = log.size() > kMaxTail ? log.substr(log.size() - kMaxTail) : log;
    }

    ImGui::TextColored(Theme::kTextMuted, "Live output");
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushFont(Theme::LoadedFonts.mono);
    BeginAltCardStyle();
    ImGui::BeginChild("ram_log", ImVec2(width, Theme::Scale(150.0f)), true, ImGuiWindowFlags_HorizontalScrollbar);
    if (tail.empty()) {
        ImGui::TextColored(Theme::kTextMuted, "Waiting for mprime output...");
    } else {
        bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
        ImGui::TextUnformatted(tail.c_str());
        if (wasAtBottom) ImGui::SetScrollHereY(1.0f); // stick to bottom unless the user scrolled up to read back
    }
    ImGui::EndChild();
    EndAltCardStyle();
    ImGui::PopFont();
}

void DrawMemorySection(Metrics& m, float width) {
    bool running = m.bench.memRunning.load(std::memory_order_acquire);
    bool haveResult = m.bench.memResult.valid;
    float h = running || !haveResult ? Theme::Scale(140.0f) : Theme::Scale(340.0f);

    if (Widgets::BeginCard("bench_mem", ImVec2(width, h), /*elevated=*/true, Theme::kAccentMemory)) {
        SectionTitle("Memory bandwidth & latency", Theme::kAccentMemory);
        ImGui::SameLine(width - Theme::kPanelPadding * 2 - Theme::Scale(80.0f));
        if (running) Widgets::StatusPill("RUNNING", Theme::kAccentMemory);
        else if (haveResult) Widgets::StatusPill("DONE", Theme::kAccentOk);
        else Widgets::StatusPill("IDLE", Theme::kTextMuted);

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextColored(Theme::kTextMuted,
            "Cache/DRAM latency and read/write/copy bandwidth. Multi-threaded, ~2-4s, pins one thread per physical core.");
        ImGui::Dummy(ImVec2(0, 8));

        // Runs inside ram_oc_daemon, which vitals never starts on its own.
        ImGui::BeginDisabled(running || !m.dramOcSupported);
        if (ImGui::Button(running ? "Running..." : "Run memory benchmark", ImVec2(Theme::Scale(200.0f), Theme::Scale(30.0f))))
            m.bench.RunMemory();
        ImGui::EndDisabled();
        if (!m.dramOcSupported) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(Theme::kAccentTemp, "Needs ram_oc_daemon \xE2\x80\x94 start it in Settings \xE2\x86\x92 Daemons.");
        }

        if (!haveResult) { Widgets::EndCard(); return; }

        float innerW = width - Theme::kPanelPadding * 2;
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::TextColored(Theme::kTextMuted, "Latency");
        BeginAltCardStyle();
        ImGui::BeginChild("mem_lat", ImVec2(innerW, Theme::Scale(96.0f)), true);
        char l1[24], l2[24], l3[24], dram[24];
        std::snprintf(l1, sizeof(l1), "%.2f ns", m.bench.memResult.latL1Ns);
        std::snprintf(l2, sizeof(l2), "%.2f ns", m.bench.memResult.latL2Ns);
        std::snprintf(l3, sizeof(l3), "%.2f ns", m.bench.memResult.latL3Ns);
        std::snprintf(dram, sizeof(dram), "%.2f ns", m.bench.memResult.latDramNs);
        Row("L1", l1, innerW - Theme::kPanelPadding * 2);
        Row("L2", l2, innerW - Theme::kPanelPadding * 2);
        Row("L3", l3, innerW - Theme::kPanelPadding * 2);
        Row("DRAM", dram, innerW - Theme::kPanelPadding * 2);
        ImGui::EndChild();
        EndAltCardStyle();

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextColored(Theme::kTextMuted, "Bandwidth");
        BeginAltCardStyle();
        ImGui::BeginChild("mem_bw", ImVec2(innerW, Theme::Scale(76.0f)), true);
        char rd[24], wr[24], cp[24];
        std::snprintf(rd, sizeof(rd), "%.0f MB/s", m.bench.memResult.bwReadMBs);
        std::snprintf(wr, sizeof(wr), "%.0f MB/s", m.bench.memResult.bwWriteMBs);
        std::snprintf(cp, sizeof(cp), "%.0f MB/s", m.bench.memResult.bwCopyMBs);
        Row("Read", rd, innerW - Theme::kPanelPadding * 2);
        Row("Write", wr, innerW - Theme::kPanelPadding * 2);
        Row("Copy", cp, innerW - Theme::kPanelPadding * 2);
        ImGui::EndChild();
        EndAltCardStyle();
    }
    Widgets::EndCard();
}

void DrawCpuSection(Metrics& m, float width) {
    bool running = m.bench.cpuRunning.load(std::memory_order_acquire);
    bool haveResult = m.bench.cpuResult.valid;
    float h = haveResult && !running ? Theme::Scale(190.0f) : Theme::Scale(140.0f);

    if (Widgets::BeginCard("bench_cpu", ImVec2(width, h), /*elevated=*/true, Theme::kAccentCpu)) {
        SectionTitle("CPU throughput", Theme::kAccentCpu);
        ImGui::SameLine(width - Theme::kPanelPadding * 2 - Theme::Scale(80.0f));
        if (running) Widgets::StatusPill("RUNNING", Theme::kAccentCpu);
        else if (haveResult) Widgets::StatusPill("DONE", Theme::kAccentOk);
        else Widgets::StatusPill("IDLE", Theme::kTextMuted);

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextColored(Theme::kTextMuted,
            "Floating-point throughput across all logical cores, ~1.5s. A relative score for "
            "before/after comparisons on this machine, not a Cinebench-style absolute number.");
        ImGui::Dummy(ImVec2(0, 8));

        ImGui::BeginDisabled(running);
        if (ImGui::Button(running ? "Running..." : "Run CPU benchmark", ImVec2(Theme::Scale(200.0f), Theme::Scale(30.0f))))
            m.bench.RunCpu(m.logicalProcessors);
        ImGui::EndDisabled();

        if (haveResult) {
            float innerW = width - Theme::kPanelPadding * 2;
            ImGui::Dummy(ImVec2(0, 10));
            BeginAltCardStyle();
            ImGui::BeginChild("cpu_score", ImVec2(innerW, Theme::Scale(36.0f)), true);
            char score[32]; std::snprintf(score, sizeof(score), "%.0f Mops/s", m.bench.cpuResult.scoreMops);
            Row("Score", score, innerW - Theme::kPanelPadding * 2);
            ImGui::EndChild();
            EndAltCardStyle();
        }
    }
    Widgets::EndCard();
}

void DrawGpuSection(Metrics& m, float width) {
    bool running = m.bench.gpuStressUntilT > m.t;
    static int durationSec = 15;
    static int loadPct = 100;

    if (Widgets::BeginCard("bench_gpu", ImVec2(width, running ? Theme::Scale(220.0f) : Theme::Scale(190.0f)), /*elevated=*/true, Theme::kAccentGpu)) {
        SectionTitle("GPU load", Theme::kAccentGpu);
        ImGui::SameLine(width - Theme::kPanelPadding * 2 - Theme::Scale(80.0f));
        if (running) Widgets::StatusPill("RUNNING", Theme::kAccentGpu);
        else Widgets::StatusPill("IDLE", Theme::kTextMuted);

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextColored(Theme::kTextMuted,
            "Draws extra overdraw each frame to load the GPU (no scored benchmark — watch the "
            "GPU panel's live usage/temp on the Performance page while this runs).");
        ImGui::Dummy(ImVec2(0, 8));

        float innerW = std::min(width - Theme::kPanelPadding * 2, Theme::Scale(220.0f));
        ImGui::PushID("gpu_stress");
        ImGui::BeginDisabled(running);
        ImGui::SetNextItemWidth(innerW);
        ImGui::SliderInt("Duration (s)", &durationSec, 5, 120);
        ImGui::SetNextItemWidth(innerW);
        ImGui::SliderInt("Load (%)", &loadPct, 1, 100);
        ImGui::EndDisabled();
        if (running) {
            if (ImGui::Button("Stop", ImVec2(Theme::Scale(140.0f), Theme::Scale(30.0f)))) m.bench.StopGpuStress(m.t);
        } else {
            if (ImGui::Button("Run GPU stress", ImVec2(Theme::Scale(200.0f), Theme::Scale(30.0f))))
                m.bench.RunGpuStress(m.t, (float)durationSec, loadPct);
        }
        ImGui::PopID();

        if (running) {
            ImGui::Dummy(ImVec2(0, 8));
            float remain = (float)(m.bench.gpuStressUntilT - m.t);
            float frac = durationSec > 0 ? 1.0f - remain / (float)durationSec : 0.0f;
            ElapsedBar(frac, Theme::kAccentGpu, width - Theme::kPanelPadding * 2);
            char buf[32]; std::snprintf(buf, sizeof(buf), "%.0fs remaining", remain);
            ImGui::TextColored(Theme::kTextMuted, "%s", buf);
        }
    }
    Widgets::EndCard();
}

void DrawStressSection(Metrics& m, float width) {
    bool running = m.bench.stressRunning.load(std::memory_order_acquire);
    static int durationSec = 60;
    static int loadPct = 100;

    if (Widgets::BeginCard("bench_stress", ImVec2(width, running ? Theme::Scale(220.0f) : Theme::Scale(190.0f)), /*elevated=*/true, Theme::kAccentEnergy)) {
        SectionTitle("CPU stress test", Theme::kAccentEnergy);
        ImGui::SameLine(width - Theme::kPanelPadding * 2 - Theme::Scale(80.0f));
        if (running) Widgets::StatusPill("RUNNING", Theme::kAccentEnergy);
        else Widgets::StatusPill("IDLE", Theme::kTextMuted);

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextColored(Theme::kTextMuted,
            "All logical cores, duty-cycled to the configured load. Watch the CPU/Thermals pages' "
            "live telemetry while it runs instead of a separate report here.");
        ImGui::Dummy(ImVec2(0, 8));

        float innerW = std::min(width - Theme::kPanelPadding * 2, Theme::Scale(220.0f));
        ImGui::PushID("stress_test");
        ImGui::BeginDisabled(running);
        ImGui::SetNextItemWidth(innerW);
        ImGui::SliderInt("Duration (s)", &durationSec, 5, 600);
        ImGui::SetNextItemWidth(innerW);
        ImGui::SliderInt("Load (%)", &loadPct, 1, 100);
        ImGui::EndDisabled();
        if (running) {
            if (ImGui::Button("Stop", ImVec2(Theme::Scale(140.0f), Theme::Scale(30.0f)))) m.bench.StopStress();
        } else {
            if (ImGui::Button("Start stress test", ImVec2(Theme::Scale(200.0f), Theme::Scale(30.0f))))
                m.bench.RunStress((float)durationSec, loadPct, m.logicalProcessors);
        }
        ImGui::PopID();

        if (running) {
            ImGui::Dummy(ImVec2(0, 8));
            float remain = m.bench.stressRemainingSec.load(std::memory_order_relaxed);
            float frac = durationSec > 0 ? 1.0f - remain / (float)durationSec : 0.0f;
            ElapsedBar(frac, Theme::kAccentEnergy, width - Theme::kPanelPadding * 2);
            char buf[32]; std::snprintf(buf, sizeof(buf), "%.0fs remaining", remain);
            ImGui::TextColored(Theme::kTextMuted, "%s", buf);
        }
    }
    Widgets::EndCard();
}

void DrawRamTestSection(Metrics& m, float width) {
    bool running = m.bench.ramRunning.load(std::memory_order_acquire);
    static int durationSec = 300;
    float h = running ? Theme::Scale(400.0f) : Theme::Scale(170.0f);

    if (Widgets::BeginCard("bench_ram", ImVec2(width, h), /*elevated=*/true, Theme::kAccentMemory)) {
        SectionTitle("RAM stability test", Theme::kAccentMemory);
        ImGui::SameLine(width - Theme::kPanelPadding * 2 - Theme::Scale(100.0f));
        if (running) Widgets::StatusPill("RUNNING", Theme::kAccentMemory);
        else if (!m.bench.ramResult.valid) Widgets::StatusPill("IDLE", Theme::kTextMuted);
        else if (m.bench.ramResult.toolMissing) Widgets::StatusPill("NOT FOUND", Theme::kAccentClock);
        else if (m.bench.ramResult.errors == 0) Widgets::StatusPill("STABLE", Theme::kAccentOk);
        else Widgets::StatusPill("ERRORS", Theme::kAccentClock);

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextColored(Theme::kTextMuted,
            "Prime95 torture test (bundled mprime, all cores + most of RAM) - a stability check, "
            "not a scored benchmark. Errors mean a hardware fault was caught.");
        ImGui::Dummy(ImVec2(0, 8));

        float innerW = width - Theme::kPanelPadding * 2;
        ImGui::PushID("ram_test");
        ImGui::BeginDisabled(running);
        ImGui::SetNextItemWidth(std::min(innerW, Theme::Scale(220.0f)));
        ImGui::SliderInt("Duration (s)", &durationSec, 30, 3600);
        ImGui::EndDisabled();
        if (running) {
            if (ImGui::Button("Stop", ImVec2(Theme::Scale(140.0f), Theme::Scale(30.0f)))) m.bench.StopRamTest();
        } else {
            if (ImGui::Button("Start RAM test", ImVec2(Theme::Scale(200.0f), Theme::Scale(30.0f))))
                m.bench.RunRamTest((float)durationSec, m.memTotalGB);
        }
        ImGui::PopID();

        if (running) {
            float remain = m.bench.ramRemainingSec.load(std::memory_order_relaxed);
            int liveErrors = m.bench.ramLiveErrors.load(std::memory_order_relaxed);
            float frac = durationSec > 0 ? 1.0f - remain / (float)durationSec : 0.0f;

            ImGui::Dummy(ImVec2(0, 8));
            ElapsedBar(frac, liveErrors == 0 ? Theme::kAccentMemory : Theme::kAccentClock, innerW);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.0fs remaining", remain);
            ImGui::TextColored(Theme::kTextMuted, "%s", buf);
            ImGui::SameLine();
            if (liveErrors == 0) ImGui::TextColored(Theme::kAccentOk, "- 0 errors so far");
            else { char eb[32]; std::snprintf(eb, sizeof(eb), "- %d error(s) so far", liveErrors); ImGui::TextColored(Theme::kAccentClock, "%s", eb); }

            ImGui::Dummy(ImVec2(0, 8));
            DrawLiveLog(innerW);
        } else if (m.bench.ramResult.valid && !m.bench.ramResult.toolMissing) {
            ImGui::Dummy(ImVec2(0, 4));
            if (m.bench.ramResult.errors == 0) {
                ImGui::TextColored(Theme::kAccentOk, "Stable - 0 errors detected.");
            } else {
                char buf[48]; std::snprintf(buf, sizeof(buf), "%d error(s) detected.", m.bench.ramResult.errors);
                ImGui::TextColored(Theme::kAccentClock, "%s", buf);
            }
        } else if (m.bench.ramResult.valid && m.bench.ramResult.toolMissing) {
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::TextColored(Theme::kAccentClock, "mprime not found under oc/p95v3019b20.linux64/");
        }
    }
    Widgets::EndCard();
}

} // namespace

void DrawBenchmarksPage(Metrics& m, ImVec2 size) {
    ImGui::PushFont(Theme::LoadedFonts.heading);
    ImGui::TextColored(Theme::kTextPrimary, "Benchmarks");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextColored(Theme::kTextMuted, "Relative scores for before/after comparisons on this machine, plus stability checks that watch for hardware faults.");
    ImGui::Dummy(ImVec2(0, 10));

    float colW = std::min((size.x - Theme::kGap) * 0.5f, Theme::Scale(420.0f));

    ImGui::BeginChild("bench_left", ImVec2(colW, size.y - Theme::Scale(30.0f)), false);
    DrawMemorySection(m, colW);
    ImGui::Dummy(ImVec2(0, Theme::kGap));
    DrawCpuSection(m, colW);
    ImGui::Dummy(ImVec2(0, Theme::kGap));
    DrawGpuSection(m, colW);
    ImGui::EndChild();

    ImGui::SameLine(0, Theme::kGap);
    ImGui::BeginChild("bench_right", ImVec2(colW, size.y - Theme::Scale(30.0f)), false);
    DrawStressSection(m, colW);
    ImGui::Dummy(ImVec2(0, Theme::kGap));
    DrawRamTestSection(m, colW);
    ImGui::EndChild();
}
