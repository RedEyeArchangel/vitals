#pragma once

#include <string>

// Full per-process detail for the Processes page's "Properties" popup.
// Deliberately NOT part of ProcessRow/Metrics::Update() — reading
// smaps_rollup/cmdline/cgroup/wchan for every process every tick would be
// wasteful when the popup only ever shows one process. Read on demand
// instead via ReadProcessDetail() (see metrics/process_detail.cpp), only
// while it's open.
struct ProcessDetail {
    bool found = false; // false if the pid no longer exists
    int pid = 0;
    std::string name;
    std::string user;
    std::string status;
    std::string memoryText;   // Pss (proportional share of shared pages) if smaps_rollup is readable, else RSS
    std::string virtualText;
    std::string residentText; // RSS
    std::string writableText; // private dirty+clean pages; "N/A" if smaps_rollup unreadable
    std::string sharedText;
    float cpuPct = 0.0f; // caller-supplied — reuses the already-computed live value, no re-measurement
    std::string cpuTimeText;
    std::string startedText;
    int nice = 0;
    std::string priorityText;
    std::string securityContext;
    std::string commandLine;
    std::string waitingChannel;
    std::string controlGroup;
};

ProcessDetail ReadProcessDetail(int pid, float liveCpuPct);
