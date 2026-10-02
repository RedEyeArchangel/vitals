#include "pending_apply.h"

#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

struct Entry {
    bool pending = false;
    std::chrono::steady_clock::time_point deadline;
    bool hasConfirmed = false;
    GpuCtlConfigPOD lastConfirmed{};
    GpuCtlConfigPOD pendingConfig{};
};

std::mutex g_mu;
std::unordered_map<std::string, Entry> g_entries;

} // namespace

namespace PendingApply {

void Arm(const std::string& pciSlot, uint32_t timeoutSec, const GpuCtlConfigPOD& applied) {
    std::lock_guard<std::mutex> lock(g_mu);
    Entry& e = g_entries[pciSlot];
    e.pending = true;
    e.pendingConfig = applied;
    e.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
}

bool Confirm(const std::string& pciSlot, GpuCtlConfigPOD* out) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_entries.find(pciSlot);
    if (it == g_entries.end() || !it->second.pending) return false;
    Entry& e = it->second;
    e.pending = false;
    e.hasConfirmed = true;
    e.lastConfirmed = e.pendingConfig;
    *out = e.pendingConfig;
    return true;
}

void Cancel(const std::string& pciSlot) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_entries.find(pciSlot);
    if (it != g_entries.end()) it->second.pending = false;
}

void Forget(const std::string& pciSlot) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_entries.erase(pciSlot);
}

void SeedConfirmed(const std::string& pciSlot, const GpuCtlConfigPOD& cfg) {
    std::lock_guard<std::mutex> lock(g_mu);
    Entry& e = g_entries[pciSlot];
    e.hasConfirmed = true;
    e.lastConfirmed = cfg;
}

bool IsPending(const std::string& pciSlot) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_entries.find(pciSlot);
    return it != g_entries.end() && it->second.pending;
}

bool GetConfirmed(const std::string& pciSlot, GpuCtlConfigPOD* out) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_entries.find(pciSlot);
    if (it == g_entries.end() || !it->second.hasConfirmed) return false;
    *out = it->second.lastConfirmed;
    return true;
}

void CheckExpirations(const RevertFn& revertFn) {
    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> expired;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        for (auto& [slot, e] : g_entries) {
            if (e.pending && now >= e.deadline) expired.push_back(slot);
        }
    }
    for (const auto& slot : expired) {
        bool hasConfirmed; GpuCtlConfigPOD cfg;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            Entry& e = g_entries[slot];
            e.pending = false;
            hasConfirmed = e.hasConfirmed;
            cfg = e.lastConfirmed;
        }
        revertFn(slot, hasConfirmed, cfg); // outside the lock: calls back into backend sysfs/NVML writes
    }
}

} // namespace PendingApply
