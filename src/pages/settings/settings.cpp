#include "pages/settings/settings.h"
#include "pages/performance/shared.h" // BeginAltCardStyle/EndAltCardStyle
#include "theme.h"
#include "widgets.h"
#include "metrics/desktop_apps.h" // IsVitalsAutostartEnabled/SetVitalsAutostartEnabled
#include "dram_oc/daemon_ipc.h"   // GetRamOcDaemonStatus/RamOcDaemonPath
#include "logging.h"
#include "tray_icon.h"

#include <algorithm>
#include <cstring>
#include <grp.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// Settings, laid out like a system-settings app: titled groups, each a card
// of rows — title + one-line description on the left, the control
// right-aligned — using the same toggles/segmented buttons as the GPU page.

namespace {

// Double fork so xdg-open is reparented to init and never lingers as a zombie of vitals.
void OpenLogFile(const std::string& path) {
    pid_t pid = fork();
    if (pid == 0) {
        if (fork() == 0) {
            execlp("xdg-open", "xdg-open", path.c_str(), (char*)nullptr);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0) waitpid(pid, nullptr, 0);
}

bool FileExists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

bool InGroup(const char* name) {
    struct group* gr = getgrnam(name);
    if (!gr) return false;
    if (getegid() == gr->gr_gid) return true;
    gid_t groups[256];
    int n = getgroups(256, groups);
    for (int i = 0; i < n; ++i) if (groups[i] == gr->gr_gid) return true;
    return false;
}

// ---- layout primitives ------------------------------------------------------

// Small muted caption above a card, e.g. "APPEARANCE".
void GroupTitle(const char* title, const char* subtitle = nullptr) {
    ImGui::Dummy(ImVec2(0, Theme::Scale(18.0f)));
    ImGui::PushFont(Theme::LoadedFonts.bodyBold);
    ImGui::TextColored(Theme::kTextPrimary, "%s", title);
    ImGui::PopFont();
    if (subtitle) ImGui::TextColored(Theme::kTextMuted, "%s", subtitle);
    ImGui::Dummy(ImVec2(0, Theme::Scale(4.0f)));
}

void BeginGroupCard(const char* id, float width) {
    BeginAltCardStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Theme::Scale(18.0f), Theme::Scale(6.0f)));
    ImGui::BeginChild(id, ImVec2(width, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
}
void EndGroupCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    EndAltCardStyle();
}

// One settings row: title/description left, then the caller draws its
// control right after this returns (cursor is placed so a control of
// `controlW` ends at the card's right edge, vertically centered).
// `first` suppresses the divider line above the first row of a card.
void Row(const char* title, const char* desc, float controlW, bool first = false) {
    float avail = ImGui::GetContentRegionAvail().x;
    if (!first) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + avail, p.y), ImGui::GetColorU32(Theme::kBorder));
    }
    ImGui::Dummy(ImVec2(0, Theme::Scale(8.0f)));
    float top = ImGui::GetCursorPosY();
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + avail - controlW - Theme::Scale(24.0f));
    ImGui::TextColored(Theme::kTextPrimary, "%s", title);
    if (desc && desc[0]) ImGui::TextColored(Theme::kTextMuted, "%s", desc);
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    float textH = ImGui::GetItemRectSize().y;
    float bottom = ImGui::GetCursorPosY();
    // Control: right-aligned, vertically centered on the text block.
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + avail - controlW,
                               top + std::max(0.0f, (textH - ImGui::GetFrameHeight()) * 0.5f)));
    ImGui::SetNextItemWidth(controlW);
    // Leave the cursor below whichever is taller once the control is drawn.
    ImGui::GetStateStorage()->SetFloat(ImGui::GetID("##rowbottom"), bottom);
}
void RowEnd() {
    float bottom = ImGui::GetStateStorage()->GetFloat(ImGui::GetID("##rowbottom"), ImGui::GetCursorPosY());
    if (ImGui::GetCursorPosY() < bottom) ImGui::SetCursorPosY(bottom);
    ImGui::Dummy(ImVec2(0, Theme::Scale(6.0f)));
}

float ToggleW() { return ImGui::GetFrameHeight() * 1.9f; }

void CommandRow(const char* id, const std::string& cmd) {
    ImGui::PushID(id);
    float copyW = Theme::Scale(80.0f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - copyW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::PushFont(Theme::LoadedFonts.mono);
    ImGui::InputText("##cmd", const_cast<char*>(cmd.c_str()), cmd.size() + 1, ImGuiInputTextFlags_ReadOnly);
    ImGui::PopFont();
    ImGui::SameLine();
    if (ImGui::Button("Copy", ImVec2(copyW, 0))) ImGui::SetClipboardText(cmd.c_str());
    ImGui::PopID();
}

void Hint(const std::string& text) { ImGui::TextColored(Theme::kTextSecondary, "%s", text.c_str()); }

// Card header line for one daemon: name left, status pill(s) right.
void DaemonHeader(const char* name, const char* desc, const char* status, ImVec4 statusColor,
                  const char* extra = nullptr, ImVec4 extraColor = ImVec4()) {
    ImGui::Dummy(ImVec2(0, Theme::Scale(8.0f)));
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(Theme::LoadedFonts.bodyBold);
    ImGui::TextColored(Theme::kTextPrimary, "%s", name);
    ImGui::PopFont();
    // Same geometry as Widgets::StatusPill (padding 10, dot 7, gap 8 → text + 35px).
    auto pillWidth = [](const char* t) { return ImGui::CalcTextSize(t).x + 35.0f; };
    float pillW = pillWidth(status) + (extra ? pillWidth(extra) + ImGui::GetStyle().ItemSpacing.x : 0.0f);
    ImGui::SameLine(avail - pillW);
    if (extra) { Widgets::StatusPill(extra, extraColor); ImGui::SameLine(); }
    Widgets::StatusPill(status, statusColor);
    ImGui::TextColored(Theme::kTextMuted, "%s", desc);
    ImGui::Dummy(ImVec2(0, Theme::Scale(6.0f)));
}

// ---- groups -------------------------------------------------------------------

void DrawAppearance(float width) {
    GroupTitle("Appearance");
    BeginGroupCard("##appearance", width);

    static const char* kStyleLabels[] = {"Refined Dark", "Modern SaaS", "Vivid", "Cyberpunk"};
    static const Theme::StyleDirection kStyles[] = {Theme::StyleDirection::RefinedDark, Theme::StyleDirection::ModernSaaS,
                                                   Theme::StyleDirection::VividMonitoring, Theme::StyleDirection::Cyberpunk};
    int style = 0;
    for (int i = 0; i < 4; ++i) if (Theme::CurrentStyleDirection == kStyles[i]) style = i;
    float segW = Theme::Scale(520.0f);
    Row("Theme", "Color palette and chart style for the whole app.", segW, true);
    if (Widgets::SegmentedTabs("theme", kStyleLabels, 4, &style, segW)) {
        Theme::SetStyleDirection(kStyles[style]);
        Theme::Apply(); // live context already exists here, unlike the startup call
    }
    RowEnd();

    static const char* kUnits[] = {"\xC2\xB0" "C", "\xC2\xB0" "F"};
    int unit = Theme::CurrentTempUnit == Theme::TempUnit::Celsius ? 0 : 1;
    float unitW = Theme::Scale(180.0f);
    Row("Temperature unit", "Used for every temperature reading and graph.", unitW);
    if (Widgets::SegmentedTabs("tempunit", kUnits, 2, &unit, unitW))
        Theme::CurrentTempUnit = unit == 0 ? Theme::TempUnit::Celsius : Theme::TempUnit::Fahrenheit;
    RowEnd();

    EndGroupCard();
}

void DrawMonitoring(float width) {
    GroupTitle("Monitoring");
    BeginGroupCard("##monitoring", width);

    Row("Expert mode", "Advanced telemetry (SMU voltages, DRAM timings, GPU/VRAM clocks) and the GPU Overclock / Fan Control tabs.", ToggleW(), true);
    Widgets::ToggleSwitch("##expert", &Theme::ExpertMode);
    RowEnd();

    float sliderW = Theme::Scale(320.0f);
    Row("Refresh rate", "How often metrics are sampled. Lower is smoother but costs more CPU.", sliderW);
    ImGui::SliderFloat("##refresh", &Theme::RefreshRateSec, 0.1f, 5.0f, "%.1f s");
    Theme::RefreshRateSec = std::clamp(Theme::RefreshRateSec, 0.1f, 5.0f);
    RowEnd();

    static const char* kHist[] = {"30 s", "2 min", "10 min"};
    static const float kHistSec[] = {30.0f, 120.0f, 600.0f};
    int hist = 0;
    for (int i = 0; i < 3; ++i) if (Theme::HistoryWindowSec == kHistSec[i]) hist = i;
    float histW = Theme::Scale(300.0f);
    Row("History window", "Time span shown by every history graph.", histW);
    if (Widgets::SegmentedTabs("history", kHist, 3, &hist, histW)) Theme::HistoryWindowSec = kHistSec[hist];
    RowEnd();

    Row("VSync", "Sync drawing to the display refresh. Off lowers latency, raises GPU use.", ToggleW());
    Widgets::ToggleSwitch("##vsync", &Theme::VSyncEnabled);
    RowEnd();

    EndGroupCard();
}

void DrawWindowStartup(float width) {
    GroupTitle("Window & startup");
    BeginGroupCard("##window", width);

    Row("Always on top", "Keep the vitals window above other windows.", ToggleW(), true);
    Widgets::ToggleSwitch("##ontop", &Theme::AlwaysOnTop);
    RowEnd();

    static bool autostart = IsVitalsAutostartEnabled(); // read once, on first draw
    Row("Launch at login", "Start vitals automatically when you log in.", ToggleW());
    if (Widgets::ToggleSwitch("##autostart", &autostart)) {
        SetVitalsAutostartEnabled(autostart);
        autostart = IsVitalsAutostartEnabled(); // reflect what actually happened, not just the click
    }
    RowEnd();

    if (!TrayIcon::Available()) {
        Row("Tray icon", "Not available \xE2\x80\x94 GTK3/appindicator wasn't found at build time, so closing the window quits vitals.", 0.0f);
        RowEnd();
    }
    EndGroupCard();
}

void DrawLogging(float width) {
    GroupTitle("Logging");
    BeginGroupCard("##logging", width);

    Row("Enable logging", "Write diagnostics (daemon and sensor errors) to a log file.", ToggleW(), true);
    Widgets::ToggleSwitch("##logging", &Theme::LoggingEnabled);
    RowEnd();

    std::string logPath = Logging::LogPath();
    float openW = Theme::Scale(130.0f);
    Row("Log file", logPath.c_str(), openW);
    if (ImGui::Button("Open", ImVec2(openW, 0))) OpenLogFile(logPath);
    RowEnd();

    EndGroupCard();
}

struct ServiceState { bool installed = false, enabled = false; };

// systemd unit state from the filesystem (no D-Bus / no `systemctl` spawn):
// installed = unit file present, enabled = linked into multi-user.target.wants.
ServiceState ReadService(const char* unit) {
    std::string u = std::string(unit) + ".service";
    ServiceState s;
    for (const char* dir : {"/etc/systemd/system/", "/usr/lib/systemd/system/", "/lib/systemd/system/"})
        if (FileExists(dir + u)) s.installed = true;
    s.enabled = FileExists("/etc/systemd/system/multi-user.target.wants/" + u);
    return s;
}

// The repo's install script, if this vitals runs from a source checkout's build dir.
std::string InstallScriptCommand() {
    std::string exe = RamOcDaemonPath();
    if (exe.find('/') == std::string::npos) return "";
    std::string dir = exe.substr(0, exe.find_last_of('/'));
    std::string repo = dir.substr(0, dir.find_last_of('/'));
    if (!FileExists(repo + "/scripts/install-daemons.sh")) return "";
    return "sudo " + repo + "/scripts/install-daemons.sh " + dir;
}

void ServiceCard(const char* id, const char* unit, const char* desc, bool running, const char* extra, ImVec4 extraColor, float width) {
    ServiceState s = ReadService(unit);
    BeginGroupCard(id, width);
    const char* status = running ? "RUNNING" : "STOPPED";
    ImVec4 statusColor = running ? Theme::kAccentOk : (s.enabled ? Theme::kAccentClock : Theme::kTextMuted);
    const char* boot = !s.installed ? "NOT INSTALLED" : (s.enabled ? "STARTS AT BOOT" : "AUTOSTART OFF");
    DaemonHeader(unit, desc, status, statusColor, extra ? extra : boot, extra ? extraColor : (s.enabled ? Theme::kAccentGpu : Theme::kTextMuted));
    std::string u = unit;
    if (!s.installed) {
        Hint("Not installed yet \xE2\x80\x94 use the one-time setup command above.");
    } else if (s.enabled) {
        if (!running) Hint("Enabled but not running \xE2\x80\x94 check `systemctl status " + u + "` / `journalctl -u " + u + "`.");
        Hint("Turn off (stops now and no longer starts at boot):");
        CommandRow((u + "_off").c_str(), "sudo systemctl disable --now " + u);
    } else {
        Hint(running ? "Running, but won't start at boot. Turn on autostart:" : "Turn on (starts now and at every boot):");
        CommandRow((u + "_on").c_str(), "sudo systemctl enable --now " + u);
    }
    ImGui::Dummy(ImVec2(0, Theme::Scale(10.0f)));
    EndGroupCard();
    ImGui::Dummy(ImVec2(0, Theme::Scale(8.0f)));
}

void DrawDaemons(const Metrics& m, float width) {
    GroupTitle("Daemons", "Optional root helpers. Like LACT, you activate them once; after that they start at every boot until you turn them off.");

    bool gpudRunning = m.gpuCtl.daemonReachable;
    DaemonStatus rs = GetRamOcDaemonStatus();
    if (!ReadService("vitals-ramocd").installed || !ReadService("vitals-gpud").installed) {
        BeginGroupCard("##setup", width);
        ImGui::Dummy(ImVec2(0, Theme::Scale(8.0f)));
        ImGui::PushFont(Theme::LoadedFonts.bodyBold);
        ImGui::TextColored(Theme::kTextPrimary, "One-time setup");
        ImGui::PopFont();
        Hint("Installs both daemons as system services, starts them, and enables them at boot:");
        std::string script = InstallScriptCommand();
        CommandRow("setup", script.empty() ? "sudo systemctl enable --now vitals-ramocd vitals-gpud" : script);
        ImGui::Dummy(ImVec2(0, Theme::Scale(10.0f)));
        EndGroupCard();
        ImGui::Dummy(ImVec2(0, Theme::Scale(8.0f)));
    }

    ServiceCard("##ramoc", "vitals-ramocd", "RAM timings, SMU voltages and clocks (Expert mode) and the memory benchmark.",
                rs.running, rs.running ? (m.dramOc.smuSupported ? "SMU ACCESS" : "NO SMU ACCESS") : nullptr,
                m.dramOc.smuSupported ? Theme::kAccentOk : Theme::kAccentTemp, width);
    ServiceCard("##gpud", "vitals-gpud", "GPU overclocking and fan control (Expert mode \xE2\x86\x92 Performance \xE2\x86\x92 GPU).",
                gpudRunning, nullptr, ImVec4(), width);

    bool socketThere = FileExists("/run/vitals_gpud_v" VITALS_GPUCTL_IPC_VERSION_STR ".sock");
    if (socketThere && !InGroup("vitals-gpu")) {
        ImGui::TextColored(Theme::kAccentTemp, "vitals-gpud runs, but your user can't reach it yet. Join its group, then log out and back in:");
        CommandRow("gpud_group", "sudo usermod -aG vitals-gpu $USER");
    }
    if (FileExists("/run/lactd.sock")) {
        ImGui::Dummy(ImVec2(0, Theme::Scale(4.0f)));
        Widgets::StatusPill("LACT RUNNING", Theme::kAccentTemp);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(Theme::kAccentTemp, "LACT controls the same GPU settings \xE2\x80\x94 use one of them:");
        CommandRow("lact_stop", "sudo systemctl disable --now lactd");
    }
}

void DrawReset(float width) {
    GroupTitle("Reset");
    BeginGroupCard("##reset", width);
    float btnW = Theme::Scale(190.0f);
    Row("Reset to defaults", "Restore theme, units, refresh rate, history window and all toggles. Daemons are not touched.", btnW, true);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(Theme::kAccentClock.x, Theme::kAccentClock.y, Theme::kAccentClock.z, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::kAccentClock);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    if (ImGui::Button("Reset to defaults", ImVec2(btnW, 0))) {
        Theme::ExpertMode = false;
        Theme::RefreshRateSec = 0.5f;
        Theme::VSyncEnabled = true;
        Theme::AlwaysOnTop = false;
        Theme::CurrentTempUnit = Theme::TempUnit::Celsius;
        Theme::HistoryWindowSec = 30.0f;
        Theme::LoggingEnabled = false;
        Theme::SetStyleDirection(Theme::StyleDirection::RefinedDark);
        Theme::Apply();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    RowEnd();
    EndGroupCard();
}

} // namespace

void DrawSettingsPage(const Metrics& m, ImVec2 /*size*/) {
    // A readable column, not edge-to-edge on wide screens.
    float width = std::min(ImGui::GetContentRegionAvail().x, Theme::Scale(1100.0f));

    ImGui::PushFont(Theme::LoadedFonts.heading);
    ImGui::TextColored(Theme::kTextPrimary, "Settings");
    ImGui::PopFont();
    ImGui::TextColored(Theme::kTextMuted, "Changes apply immediately and are saved when vitals exits.");

    DrawAppearance(width);
    DrawMonitoring(width);
    DrawWindowStartup(width);
    DrawLogging(width);
    DrawDaemons(m, width);
    DrawReset(width);
    ImGui::Dummy(ImVec2(0, Theme::Scale(20.0f)));
}
