#include "metrics/system_identity.h"
#include "metrics/proc_util.h"

#include <pwd.h>
#include <sys/utsname.h>
#include <unistd.h>

void ReadSystemIdentity(std::string& kernel, std::string& hostname, std::string& user,
                         std::string& os, std::string& platform) {
    struct utsname uts;
    if (uname(&uts) == 0) {
        kernel = uts.release;
        hostname = uts.nodename;
        platform = uts.machine;
    }

    struct passwd* pw = getpwuid(geteuid());
    if (pw && pw->pw_name) user = pw->pw_name;

    for (const std::string& line : ReadLines("/etc/os-release")) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            std::string val = line.substr(12);
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
            os = val;
            break;
        }
    }
}
