#pragma once

#include "metrics.h" // ServiceUnit

#include <vector>

// systemd units via `systemctl list-units`. Backs the Services page.
//
// ponytail: synchronous `systemctl` call on the caller's thread — Metrics
// throttles calls to every few seconds (see servicesRefreshT_) so it can't
// visibly stutter the UI; move to a background thread if it ever does on
// slower systems.
std::vector<ServiceUnit> ReadServices();
