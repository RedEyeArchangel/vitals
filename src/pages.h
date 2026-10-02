#pragma once

#include "imgui.h"
#include "metrics.h"
#include "pages/summary/summary.h"
#include "pages/performance/performance.h"
#include "pages/processes/processes.h"
#include "pages/system_info/system_info.h"
#include "pages/startup_apps/startup_apps.h"
#include "pages/users/users.h"
#include "pages/services/services.h"
#include "pages/benchmarks/benchmarks.h"
#include "pages/installed_apps/installed_apps.h"
#include "pages/disk_space/disk_space.h"
#include "pages/settings/settings.h"

enum class Page { Summary, Performance, Processes, SystemInfo,
                   StartupApps, Users, Services, Benchmarks, InstalledApps, DiskSpace,
                   Settings };
