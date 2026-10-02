#include "nvidia_nvapi_shim.h"

#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <vector>

// ---- NvAPI wire types, ported field-for-field from LACT's
// lact-daemon/src/server/gpu_controller/nvidia/nvapi.rs (`#[repr(C)]`
// structs there == plain C struct layout on this ABI) ----------------------
namespace {

using NvU8 = uint8_t;
using NvU32 = uint32_t;
using NvS32 = int32_t;
using NvAPI_Status = int32_t;
using RawNvApiGpuHandle = void*;

constexpr uint32_t kMaxPhysicalGpus = 64; // NVAPI_MAX_PHYSICAL_GPUS
constexpr uint32_t kShortStringMax = 64;  // NVAPI_SHORT_STRING_MAX

constexpr uint32_t kQueryInitialize = 0x0150e828;
constexpr uint32_t kQueryEnumPhysicalGpus = 0xe5ac921f;
constexpr uint32_t kQueryGetBusId = 0x1be0b8e5;
constexpr uint32_t kQueryGetErrorMessage = 0x6c2d048c;
constexpr uint32_t kQueryVoltRailsGetControl = 0x9df23ca1;
constexpr uint32_t kQueryVoltRailsSetControl = 0xb9306d9b;
constexpr uint32_t kQueryClkVfPointsGetInfo = 0x507b4b59;
constexpr uint32_t kQueryClkVfPointsSetControl = 0x733e009;
constexpr uint32_t kQueryClkVfPointsGetControl = 0x23f1b133;

constexpr uint32_t kClkVfPointTypeProg = 0; // CLOCK_CLIENT_CLK_VF_POINT_TYPE_PROG

constexpr uint32_t MakeVersion(size_t structSize, int version) {
    return (uint32_t)(structSize | ((uint32_t)version << 16));
}

// NvAPI's wire structs use ordinary (natural, 4-byte) alignment — same as
// LACT's plain `#[repr(C)]` on the Rust side, so no #pragma pack here.
struct ClientVoltRailsControlV1 {
    NvU32 version;
    NvU8 percentDelta;
    NvU8 rsvd[32];
};

struct ClockClientClkVfPointControlProgV1 {
    NvS32 freqOffsetKhz;
};

union ClockClientClkVfPointControlDataV1 {
    ClockClientClkVfPointControlProgV1 prog;
    NvU8 rsvd[16];
};

struct ClockClientClkVfPointControlV1 {
    NvU32 type_;
    NvU8 rsvd[16];
    ClockClientClkVfPointControlDataV1 data;
};

struct ClockClientClkVfPointsControlV1 {
    NvU32 version;
    NvU32 vfPointsMask[8];
    NvU8 rsvd[32];
    ClockClientClkVfPointControlV1 vfPoints[255];
};

using QueryInterfaceFn = void* (*)(uint32_t);
using InitializeFn = NvAPI_Status (*)();
using EnumPhysicalGpusFn = NvAPI_Status (*)(RawNvApiGpuHandle handles[kMaxPhysicalGpus], uint32_t* count);
using GetBusIdFn = NvAPI_Status (*)(RawNvApiGpuHandle, uint32_t* id);
using GetErrorMessageFn = NvAPI_Status (*)(NvAPI_Status, char text[kShortStringMax]);
using PhysicalGpuQueryFn = NvAPI_Status (*)(RawNvApiGpuHandle, void* data);

} // namespace

struct NvApiGpuHandle {
    RawNvApiGpuHandle raw = nullptr;
};

namespace {
std::vector<NvApiGpuHandle> g_gpus;
QueryInterfaceFn g_queryInterface = nullptr;

void* QueryInterface(uint32_t id) {
    return g_queryInterface ? g_queryInterface(id) : nullptr;
}

bool CheckStatus(NvAPI_Status status, std::string* err) {
    if (status == 0) return true;
    if (err) {
        auto getMsg = (GetErrorMessageFn)QueryInterface(kQueryGetErrorMessage);
        char text[kShortStringMax] = {0};
        if (getMsg && getMsg(status, text) == 0) {
            *err = "NvAPI error " + std::to_string(status) + ": " + text;
        } else {
            *err = "NvAPI error " + std::to_string(status);
        }
    }
    return false;
}

} // namespace

bool NvApiShim::Load() {
    if (loaded_) return true;
    lib_ = dlopen("libnvidia-api.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!lib_) return false;

    g_queryInterface = (QueryInterfaceFn)dlsym(lib_, "nvapi_QueryInterface");
    if (!g_queryInterface) { dlclose(lib_); lib_ = nullptr; return false; }

    auto initialize = (InitializeFn)QueryInterface(kQueryInitialize);
    if (!initialize || initialize() != 0) { dlclose(lib_); lib_ = nullptr; g_queryInterface = nullptr; return false; }

    auto enumGpus = (EnumPhysicalGpusFn)QueryInterface(kQueryEnumPhysicalGpus);
    if (!enumGpus) { dlclose(lib_); lib_ = nullptr; g_queryInterface = nullptr; return false; }

    RawNvApiGpuHandle raw[kMaxPhysicalGpus] = {nullptr};
    uint32_t count = 0;
    if (enumGpus(raw, &count) != 0) { dlclose(lib_); lib_ = nullptr; g_queryInterface = nullptr; return false; }

    g_gpus.clear();
    for (uint32_t i = 0; i < count && i < kMaxPhysicalGpus; ++i) g_gpus.push_back(NvApiGpuHandle{raw[i]});

    loaded_ = true;
    return true;
}

NvApiGpuHandle* NvApiShim::FindMatchingGpu(uint32_t pciBus) {
    if (!loaded_) return nullptr;
    auto getBusId = (GetBusIdFn)QueryInterface(kQueryGetBusId);
    if (!getBusId) return nullptr;
    for (auto& gpu : g_gpus) {
        uint32_t id = 0;
        if (getBusId(gpu.raw, &id) == 0 && id == pciBus) return &gpu;
    }
    return nullptr;
}

bool NvApiShim::SetVoltageBoostPercent(NvApiGpuHandle* gpu, uint8_t percent, std::string* err) {
    if (!loaded_ || !gpu) { if (err) *err = "NvAPI shim not loaded"; return false; }
    auto setControl = (PhysicalGpuQueryFn)QueryInterface(kQueryVoltRailsSetControl);
    if (!setControl) { if (err) *err = "NvAPI: volt rails control not available on this driver"; return false; }

    ClientVoltRailsControlV1 data{};
    data.version = MakeVersion(sizeof(ClientVoltRailsControlV1), 1);
    data.percentDelta = percent;

    return CheckStatus(setControl(gpu->raw, &data), err);
}

bool NvApiShim::SetVfCurveOffsets(NvApiGpuHandle* gpu, const GpuCtlNvidiaVfCurvePointPOD* points, uint32_t count, std::string* err) {
    if (!loaded_ || !gpu) { if (err) *err = "NvAPI shim not loaded"; return false; }
    auto setControl = (PhysicalGpuQueryFn)QueryInterface(kQueryClkVfPointsSetControl);
    if (!setControl) { if (err) *err = "NvAPI: VF-point control not available on this driver"; return false; }

    // Heap-allocated: ClockClientClkVfPointsControlV1 is ~9KB (255 points),
    // too large to comfortably keep on the stack of a request-handling thread.
    auto data = std::make_unique<ClockClientClkVfPointsControlV1>();
    std::memset(data.get(), 0, sizeof(*data));
    data->version = MakeVersion(sizeof(ClockClientClkVfPointsControlV1), 1);

    for (uint32_t i = 0; i < count; ++i) {
        uint8_t point = points[i].point;
        if (point >= 255) continue;
        data->vfPointsMask[point / 32] |= (1u << (point % 32));
        data->vfPoints[point].type_ = kClkVfPointTypeProg;
        data->vfPoints[point].data.prog.freqOffsetKhz = points[i].v.freqOffsetKhz;
    }

    return CheckStatus(setControl(gpu->raw, data.get()), err);
}

NvApiShim& NvApi() {
    static NvApiShim shim;
    static bool attempted = false;
    if (!attempted) { shim.Load(); attempted = true; }
    return shim;
}
