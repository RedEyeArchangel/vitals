#pragma once

#include "metrics.h" // SystemUser

#include <vector>

// Interactive accounts only (uid 0, or a real login shell) — filters out the
// system/service accounts (daemon, www-data, ...) that clutter /etc/passwd.
// Backs the Users page.
std::vector<SystemUser> ReadSystemUsers();
