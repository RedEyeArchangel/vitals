#include "metrics/desktop_apps.h"
#include "metrics/proc_util.h"

#include <algorithm>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <pwd.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

bool ParseDesktopFile(const std::string& path, DesktopApp* out, bool isAutostartFile) {
    out->path = path;
    std::istringstream ss(ReadFile(path));
    std::string line;
    bool inEntry = false, hidden = false, gnomeDisabled = false, noDisplay = false;
    while (std::getline(ss, line)) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '[') { inEntry = (line == "[Desktop Entry]"); continue; }
        if (!inEntry) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key == "Name" && out->name.empty()) out->name = val;
        else if (key == "Exec") out->exec = val;
        else if (key == "Comment") out->comment = val;
        else if (key == "Hidden") hidden = (val == "true");
        else if (key == "NoDisplay") noDisplay = (val == "true");
        else if (key == "X-GNOME-Autostart-enabled") gnomeDisabled = (val == "false");
    }
    if (out->name.empty()) return false;
    if (!isAutostartFile && noDisplay) return false;
    out->enabled = !hidden && !gnomeDisabled;
    return true;
}

void ScanDesktopDir(const std::string& dir, bool isAutostart, std::vector<DesktopApp>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name.size() <= 8 || name.compare(name.size() - 8, 8, ".desktop") != 0) continue;
        // XDG precedence: a same-named file already added from a
        // higher-priority dir (scanned earlier, e.g. ~/.config/autostart)
        // shadows this one rather than appearing as a second row.
        bool shadowed = std::any_of(out.begin(), out.end(), [&](const DesktopApp& a) {
            return a.path.size() >= name.size() &&
                   a.path.compare(a.path.size() - name.size(), name.size(), name) == 0;
        });
        if (shadowed) continue;
        DesktopApp app;
        if (ParseDesktopFile(dir + "/" + name, &app, isAutostart)) out.push_back(app);
    }
    closedir(d);
}

std::string RealHomeDir() {
    // Under `sudo ./vitals`, $HOME is root's (sudo resets it by default);
    // SUDO_USER still names the real login user, so prefer that — but only
    // when actually running as root. Trusting SUDO_USER on an unprivileged
    // run (e.g. a stale env var inherited from a parent sudo shell) would
    // misresolve whose autostart dir gets scanned/written — same guard
    // ChownToRealUser below already applies to SUDO_UID/SUDO_GID.
    if (geteuid() == 0) {
        const char* sudoUser = getenv("SUDO_USER");
        if (sudoUser) {
            struct passwd* pw = getpwnam(sudoUser);
            if (pw && pw->pw_dir) return pw->pw_dir;
        }
    }
    const char* home = getenv("HOME");
    return home ? home : "";
}

static std::string HomeAutostartDir() {
    std::string home = RealHomeDir();
    return home.empty() ? std::string() : home + "/.config/autostart";
}

static std::string BaseName(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// A file/dir created while running root (via sudo) defaults to root
// ownership, which the real user then can't edit/delete — hand it back.
void ChownToRealUser(const std::string& path) {
    if (geteuid() != 0) return;
    const char* sudoUid = getenv("SUDO_UID");
    const char* sudoGid = getenv("SUDO_GID");
    if (!sudoUid || !sudoGid) return;
    if (chown(path.c_str(), (uid_t)std::atoi(sudoUid), (gid_t)std::atoi(sudoGid)) != 0) return; // best effort
}

// Ensures `dir` (one level under $HOME/.config) and $HOME/.config itself
// exist, chowning back any directory this call creates. Generic over which
// subdir — used for both ~/.config/autostart and ~/.config/vitals.
void EnsureConfigDir(const std::string& dir) {
    std::string configDir = dir.substr(0, dir.find_last_of('/'));
    if (mkdir(configDir.c_str(), 0755) == 0) ChownToRealUser(configDir);
    if (mkdir(dir.c_str(), 0755) == 0) ChownToRealUser(dir);
}

bool WriteFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << content;
    if (!f) return false;
    ChownToRealUser(path);
    return true;
}

bool SetStartupAppEnabled(DesktopApp& app, bool enabled) {
    std::string autostartDir = HomeAutostartDir();
    if (autostartDir.empty()) return false;
    bool inPlace = app.path.compare(0, autostartDir.size(), autostartDir) == 0;
    std::string target = inPlace ? app.path : autostartDir + "/" + BaseName(app.path);

    std::istringstream ss(ReadFile(app.path));
    std::ostringstream out;
    std::string line;
    while (std::getline(ss, line)) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("Hidden=", 0) == 0 || line.rfind("X-GNOME-Autostart-enabled=", 0) == 0) continue;
        out << line << "\n";
    }
    out << "X-GNOME-Autostart-enabled=" << (enabled ? "true" : "false") << "\n";

    if (!inPlace) EnsureConfigDir(autostartDir);
    if (!WriteFile(target, out.str())) return false;
    app.path = target;
    app.enabled = enabled;
    return true;
}

bool AddStartupApp(const DesktopApp& app) {
    std::string autostartDir = HomeAutostartDir();
    if (autostartDir.empty()) return false;
    std::string target = autostartDir + "/" + BaseName(app.path);
    struct stat st;
    if (stat(target.c_str(), &st) == 0) return false; // already a startup entry

    std::string content = ReadFile(app.path);
    if (content.empty()) return false;
    EnsureConfigDir(autostartDir);
    return WriteFile(target, content);
}

bool RemoveStartupApp(const DesktopApp& app) {
    std::string autostartDir = HomeAutostartDir();
    if (autostartDir.empty() || app.path.compare(0, autostartDir.size(), autostartDir) != 0) return false;
    return unlink(app.path.c_str()) == 0;
}

static std::string VitalsAutostartPath() {
    std::string home = RealHomeDir();
    return home.empty() ? std::string() : home + "/.config/autostart/vitals.desktop";
}

bool IsVitalsAutostartEnabled() {
    std::string path = VitalsAutostartPath();
    if (path.empty()) return false;
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool SetVitalsAutostartEnabled(bool enabled) {
    std::string path = VitalsAutostartPath();
    if (path.empty()) return false;

    if (!enabled) {
        unlink(path.c_str()); // best-effort; "already gone" is the desired end state too
        return true;
    }

    // Same /proc/self/exe trick daemon_ipc.cpp's FindDaemonPath() uses to
    // locate the sibling ram_oc_daemon binary — resolves to this vitals
    // binary regardless of how it was launched/symlinked.
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return false;
    exe[n] = '\0';

    std::ostringstream out;
    out << "[Desktop Entry]\n"
        << "Type=Application\n"
        << "Name=Vitals\n"
        << "Exec=" << exe << "\n"
        << "X-GNOME-Autostart-enabled=true\n";

    EnsureConfigDir(path.substr(0, path.find_last_of('/')));
    return WriteFile(path, out.str());
}
