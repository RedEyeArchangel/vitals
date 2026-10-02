#pragma once

#include "imgui.h"
#include "metrics.h"

void DrawGpuDetail(const Metrics& m, float width);

// "GPU Controls" section: power limit / fan / clocks / VF curve, backed by
// gpu_ctl_daemon over src/gpu_ctl/gpu_ctl_ipc.h. Expert-mode-gated, called
// from DrawGpuDetail — declared here only so it's easy to find, not meant
// to be called from anywhere else.
void DrawGpuControlsSection(const Metrics& m, float width);
