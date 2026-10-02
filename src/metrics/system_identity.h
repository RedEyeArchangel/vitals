#pragma once

#include <string>

// Kernel/hostname/user/OS/platform never change while vitals runs, so
// Metrics calls this once (see Metrics::identityRead) instead of every tick.
void ReadSystemIdentity(std::string& kernel, std::string& hostname, std::string& user,
                         std::string& os, std::string& platform);
