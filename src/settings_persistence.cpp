#include "settings_persistence.h"
#include "theme.h"
#include "metrics/desktop_apps.h" // RealHomeDir/EnsureConfigDir/WriteFile — shared sudo-safe file plumbing

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

std::string ConfigFilePath() {
    std::string home = RealHomeDir();
    return home.empty() ? std::string() : home + "/.config/vitals/settings.conf";
}

} // namespace

namespace SettingsPersistence {

WindowGeometry LoadSettings() {
    WindowGeometry geo;
    Theme::SetStyleDirection(Theme::StyleDirection::RefinedDark); // default; overwritten below if a file says otherwise
    std::string path = ConfigFilePath();
    if (path.empty()) return geo;

    // Ensure the directory exists even on a fresh install with no file yet
    // — Logging::Init() (called right after this) depends on it.
    EnsureConfigDir(path.substr(0, path.find_last_of('/')));

    std::ifstream f(path);
    if (!f) return geo;

    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "expert_mode") Theme::ExpertMode = (val == "1");
        else if (key == "refresh_rate_sec") Theme::RefreshRateSec = std::clamp(std::strtof(val.c_str(), nullptr), 0.1f, 5.0f);
        else if (key == "vsync") Theme::VSyncEnabled = (val == "1");
        else if (key == "always_on_top") Theme::AlwaysOnTop = (val == "1");
        else if (key == "temp_unit") Theme::CurrentTempUnit = (val == "f") ? Theme::TempUnit::Fahrenheit : Theme::TempUnit::Celsius;
        else if (key == "history_window_sec") Theme::HistoryWindowSec = std::strtof(val.c_str(), nullptr);
        else if (key == "logging_enabled") Theme::LoggingEnabled = (val == "1");
        else if (key == "style_direction") {
            if (val == "saas") Theme::SetStyleDirection(Theme::StyleDirection::ModernSaaS);
            else if (val == "vivid") Theme::SetStyleDirection(Theme::StyleDirection::VividMonitoring);
            else if (val == "cyberpunk") Theme::SetStyleDirection(Theme::StyleDirection::Cyberpunk);
            else Theme::SetStyleDirection(Theme::StyleDirection::RefinedDark);
        }
        else if (key == "window_x") geo.x = std::atoi(val.c_str());
        else if (key == "window_y") geo.y = std::atoi(val.c_str());
        else if (key == "window_w") geo.w = std::atoi(val.c_str());
        else if (key == "window_h") geo.h = std::atoi(val.c_str());
    }
    return geo;
}

void SaveSettings(const WindowGeometry& geo) {
    std::string path = ConfigFilePath();
    if (path.empty()) return;
    EnsureConfigDir(path.substr(0, path.find_last_of('/'))); // don't rely on LoadSettings having run first

    std::ostringstream out;
    out << "expert_mode=" << (Theme::ExpertMode ? 1 : 0) << "\n"
        << "refresh_rate_sec=" << Theme::RefreshRateSec << "\n"
        << "vsync=" << (Theme::VSyncEnabled ? 1 : 0) << "\n"
        << "always_on_top=" << (Theme::AlwaysOnTop ? 1 : 0) << "\n"
        << "temp_unit=" << (Theme::CurrentTempUnit == Theme::TempUnit::Fahrenheit ? "f" : "c") << "\n"
        << "history_window_sec=" << Theme::HistoryWindowSec << "\n"
        << "logging_enabled=" << (Theme::LoggingEnabled ? 1 : 0) << "\n"
        << "style_direction=" << (Theme::CurrentStyleDirection == Theme::StyleDirection::ModernSaaS ? "saas" :
                                   Theme::CurrentStyleDirection == Theme::StyleDirection::VividMonitoring ? "vivid" :
                                   Theme::CurrentStyleDirection == Theme::StyleDirection::Cyberpunk ? "cyberpunk" : "refined") << "\n"
        << "window_x=" << geo.x << "\n"
        << "window_y=" << geo.y << "\n"
        << "window_w=" << geo.w << "\n"
        << "window_h=" << geo.h << "\n";

    WriteFile(path, out.str());
}

} // namespace SettingsPersistence
