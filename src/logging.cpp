#include "logging.h"
#include "theme.h"
#include "metrics/desktop_apps.h" // RealHomeDir/EnsureConfigDir/ChownToRealUser

#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace {
FILE* gLogFile = nullptr;
std::string gLogPath;
} // namespace

namespace Logging {

void Init() {
    std::string home = RealHomeDir();
    if (home.empty()) return;
    gLogPath = home + "/.config/vitals/vitals.log";
    if (!Theme::LoggingEnabled) return;

    EnsureConfigDir(home + "/.config/vitals");
    gLogFile = std::fopen(gLogPath.c_str(), "a");
    if (gLogFile) ChownToRealUser(gLogPath);
}

void Shutdown() {
    if (gLogFile) {
        std::fclose(gLogFile);
        gLogFile = nullptr;
    }
}

void Log(const char* fmt, ...) {
    if (!gLogFile) return;

    time_t now = time(nullptr);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    std::fprintf(gLogFile, "[%s] ", ts);

    va_list args;
    va_start(args, fmt);
    std::vfprintf(gLogFile, fmt, args);
    va_end(args);

    std::fprintf(gLogFile, "\n");
    std::fflush(gLogFile); // small/rare writes; fine to flush every line so the file is useful if the app crashes
}

std::string LogPath() { return gLogPath; }

} // namespace Logging
