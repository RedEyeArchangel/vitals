#include "metrics/system_users.h"

#include <algorithm>
#include <pwd.h>
#include <set>
#include <utmpx.h>

std::vector<SystemUser> ReadSystemUsers() {
    std::vector<SystemUser> out;
    std::set<std::string> loggedIn;
    setutxent();
    while (struct utmpx* ut = getutxent()) {
        if (ut->ut_type == USER_PROCESS && ut->ut_user[0]) loggedIn.insert(ut->ut_user);
    }
    endutxent();

    setpwent();
    while (struct passwd* pw = getpwent()) {
        std::string shell = pw->pw_shell ? pw->pw_shell : "";
        bool interactive = pw->pw_uid == 0 ||
            (!shell.empty() && shell.find("nologin") == std::string::npos && shell.find("/false") == std::string::npos);
        if (!interactive) continue;
        SystemUser u;
        u.name = pw->pw_name ? pw->pw_name : "";
        u.uid = (int)pw->pw_uid;
        u.homeDir = pw->pw_dir ? pw->pw_dir : "";
        u.shell = shell;
        u.loggedIn = loggedIn.count(u.name) > 0;
        out.push_back(u);
    }
    endpwent();
    std::sort(out.begin(), out.end(), [](const SystemUser& a, const SystemUser& b) { return a.uid < b.uid; });
    return out;
}
