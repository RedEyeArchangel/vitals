#pragma once

#include "imgui.h"
#include "metrics.h"

enum class PerfMetric { CPU, Memory, Gpu, Disks, Network, Energy, Thermals };

void DrawPerformancePage(const Metrics& m, PerfMetric& selected, ImVec2 size);
