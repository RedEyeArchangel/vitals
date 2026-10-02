#pragma once

#include "imgui.h"
#include "metrics.h"

// Non-const: the "Run" button calls m.bench.RunMemory(), which mutates
// BenchmarkController::memRunning/memResult.
void DrawBenchmarksPage(Metrics& m, ImVec2 size);
