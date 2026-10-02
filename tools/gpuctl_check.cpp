// Self-check for gpu_ctl_daemon's pure helpers: `cmake --build build --target gpuctl_check && ./build/gpuctl_check`.
// Optional arg: a sysfs device dir (e.g. /sys/class/drm/card1/device) to also parse the live files.
#include "../gpu_ctl_daemon/amd_backend.h"
#include "../gpu_ctl_daemon/sysfs_io.h"
#include "metrics/gpu_sysfs.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    // Sanitize: hostile counts get clamped, key gets terminated, NaN replaced.
    GpuCtlConfigPOD c;
    gpuctl_config_init_unset(&c);
    assert(c.clocks.maxCoreClockMhz == GPUCTL_UNSET && c.pmfw.targetTempC == GPUCTL_UNSET);
    c.fan.curveCount = 0xffffffffu; c.clocks.nvidiaVfCurveCount = 1000; c.clocks.gpuClockOffsetCount = 17;
    std::memset(c.fan.temperatureKey, 'x', sizeof(c.fan.temperatureKey));
    c.fan.staticSpeed01 = NAN; c.fan.intervalMs = 1;
    gpuctl_config_sanitize(&c);
    assert(c.fan.curveCount == GPUCTL_MAX_FAN_POINTS && c.clocks.nvidiaVfCurveCount == GPUCTL_MAX_VF_POINTS);
    assert(c.clocks.gpuClockOffsetCount == GPUCTL_MAX_PSTATES && std::strlen(c.fan.temperatureKey) == 15);
    assert(c.fan.staticSpeed01 == 0.5f && c.fan.intervalMs == 100);

    // RDNA3 pp_power_profile_mode (captured from an RX 7800 XT): sub-rows must not become modes.
    const char* rdna3 =
        "PROFILE_INDEX(NAME) CLOCK_TYPE(NAME) FPS MinActiveFreqType MinActiveFreq\n"
        " 0 BOOTUP_DEFAULT :\n"
        "                    0(       GFXCLK)       0       1       0       4     800\n"
        "                    1(         FCLK)       0       3       0       1       0\n"
        " 1 3D_FULL_SCREEN :\n"
        "                    0(       GFXCLK)       1       3       1       0       0\n"
        " 2 POWER_SAVING :\n 3 VIDEO :\n 4 VR :\n 5 COMPUTE*:\n 6 CUSTOM :\n";
    GpuCtlCapsPOD caps{};
    AmdParseProfileModes(rdna3, &caps);
    assert(caps.profileModeCount == 7);
    assert(!std::strcmp(caps.profileModeNames[1], "3D_FULL_SCREEN") && !std::strcmp(caps.profileModeNames[5], "COMPUTE"));

    // PMFW curve: 3-point user curve resampled to 5, clamped to OD_RANGE.
    GpuCtlFanConfigPOD fan{};
    fan.enabled = true; fan.mode = GPUCTL_FAN_MODE_CURVE; fan.curveCount = 3;
    fan.curve[0] = {20, 0.0f}; fan.curve[1] = {60, 0.5f}; fan.curve[2] = {100, 1.0f};
    int t[5], s[5];
    AmdPmfwCurvePoints(fan, 5, 25, 100, 15, 100, t, s);
    assert(t[0] == 25 && t[4] == 100 && s[0] == 15 && s[4] == 100);
    for (int i = 1; i < 5; ++i) assert(t[i] >= t[i - 1] && s[i] >= s[i - 1]);
    // Static 40% -> flat line, (minT, 40) then (maxT, 40).
    fan.mode = GPUCTL_FAN_MODE_STATIC; fan.staticSpeed01 = 0.4f;
    AmdPmfwCurvePoints(fan, 5, 25, 100, 15, 100, t, s);
    assert(t[0] == 25 && t[1] == 100 && s[0] == 40 && s[4] == 40);

    // UI-side baseline parsers (src/metrics/gpu_sysfs.cpp), against RX 7800 XT text.
    AmdOdState od;
    ParseOdClkVoltage("OD_SCLK:\n0: 500Mhz\n1: 2800Mhz\nOD_MCLK:\n0: 97Mhz\n1: 1295MHz\nOD_RANGE:\n"
                      "SCLK:     500Mhz       5000Mhz\nMCLK:      97Mhz       1500Mhz\n"
                      "VDDGFX_OFFSET:    -450mv          0mv\nOD_VDDGFX_OFFSET:\n-50mV\n", &od);
    assert(od.hasSclk && od.sclkMin == 500 && od.sclkMax == 2800 && od.sclkRange.max == 5000);
    assert(od.hasMclk && od.mclkMax == 1295 && od.mclkRange.min == 97);
    assert(od.hasVoltOffset && od.voltOffsetMv == -50 && od.voltOffsetRange.min == -450 && od.voltOffsetRange.max == 0);
    ParseFanCurve("OD_FAN_CURVE:\n0: 29C 15%\n1: 44C 32%\n2: 53C 50%\n3: 60C 75%\n4: 63C 100%\nOD_RANGE:\n"
                  "FAN_CURVE(hotspot temp): 25C 100C\nFAN_CURVE(fan speed): 15% 100%\n", &od);
    assert(od.hasFanCurve && od.fanCurvePoints == 5 && od.fanCurveTemp[1] == 44 && od.fanCurvePct[4] == 100);
    assert(od.fanCurveTempRange.min == 25 && od.fanCurvePctRange.min == 15);
    int v = 0; AmdRange r;
    assert(ParseFanCtrlValue("FAN_TARGET_TEMPERATURE:\n80\nOD_RANGE:\nTARGET_TEMPERATURE: 25 110\n", &v, &r) && v == 80 && r.min == 25 && r.max == 110);
    assert(ParseFanCtrlValue("OD_ACOUSTIC_LIMIT:\n3300\nOD_RANGE:\nACOUSTIC_LIMIT: 500 3300\n", &v, &r) && v == 3300 && r.min == 500);
    assert(ParseActiveProfile(" 4 VR :\n 5        COMPUTE*:\n 6 CUSTOM :\n") == 5);
    auto lv = ParseDpmLevels("S: 187Mhz *\n0: 500Mhz \n1: 2254Mhz \n");
    assert(lv.size() == 2 && lv[1] == 2254);
    int active = -1;
    lv = ParseDpmLevels("0: 96Mhz \n1: 456Mhz \n2: 772Mhz \n3: 1218Mhz *\n", &active);
    assert(lv.size() == 4 && active == 3);

    if (argc > 1) {
        GpuCtlCapsPOD live{};
        AmdParseProfileModes(SysfsReadTrimmed(std::string(argv[1]) + "/pp_power_profile_mode"), &live);
        for (uint32_t i = 0; i < live.profileModeCount; ++i) std::printf("profile %u = %s\n", i, live.profileModeNames[i]);
    }
    std::puts("gpuctl_check: OK");
    return 0;
}
