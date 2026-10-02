#include "metrics/thermal_sensors.h"
#include "metrics/proc_util.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <dirent.h>
#include <utility>

std::vector<HwmonChip> EnumerateHwmon() {
    std::vector<HwmonChip> chips;
    DIR* d = opendir("/sys/class/hwmon");
    if (!d) return chips;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string n = ent->d_name;
        if (n == "." || n == "..") continue;
        std::string base = "/sys/class/hwmon/" + n;
        std::string name = ReadTrimmed(base + "/name");
        if (!name.empty()) chips.push_back({name, base});
    }
    closedir(d);
    return chips;
}

double FirstHwmonTemp(const std::string& chipPath) {
    for (int i = 1; i <= 8; ++i) {
        std::string raw = ReadTrimmed(chipPath + "/temp" + std::to_string(i) + "_input");
        if (!raw.empty()) return std::strtod(raw.c_str(), nullptr) / 1000.0;
    }
    return -1.0;
}

double HwmonTempByNames(const std::vector<HwmonChip>& chips, const std::set<std::string>& names) {
    for (const HwmonChip& c : chips) {
        if (names.count(c.name)) {
            double t = FirstHwmonTemp(c.path);
            if (t >= 0.0) return t;
        }
    }
    return -1.0;
}

double HwmonVoltageByLabel(const std::vector<HwmonChip>& chips, const std::string& label) {
    for (const HwmonChip& c : chips) {
        for (int i = 0; i <= 16; ++i) {
            std::string l = ReadTrimmed(c.path + "/in" + std::to_string(i) + "_label");
            if (l.empty()) continue;
            std::string a = l, b = label;
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            std::transform(b.begin(), b.end(), b.begin(), ::tolower);
            if (a != b) continue;
            std::string raw = ReadTrimmed(c.path + "/in" + std::to_string(i) + "_input");
            if (raw.empty()) continue;
            return std::strtod(raw.c_str(), nullptr) / 1000.0;
        }
    }
    return -1.0;
}

double ReadFallbackThermalZoneTemp() {
    DIR* d = opendir("/sys/class/thermal");
    if (!d) return -1.0;
    struct dirent* ent;
    double result = -1.0, fallback = -1.0;
    while ((ent = readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name.rfind("thermal_zone", 0) != 0) continue;
        std::string base = "/sys/class/thermal/" + name;
        std::string type = ReadTrimmed(base + "/type");
        std::string raw = ReadTrimmed(base + "/temp");
        if (raw.empty()) continue;
        double v = std::strtod(raw.c_str(), nullptr) / 1000.0;
        if (fallback < 0.0) fallback = v;
        std::transform(type.begin(), type.end(), type.begin(), ::tolower);
        if (type.find("cpu") != std::string::npos || type.find("pkg") != std::string::npos) result = v;
    }
    closedir(d);
    return result >= 0.0 ? result : fallback;
}

std::vector<ThermalSensor> ReadThermalSensors() {
    std::vector<ThermalSensor> sensors;
    std::vector<HwmonChip> chips = EnumerateHwmon();
    for (const HwmonChip& chip : chips) {
        for (int i = 1; i <= 16; ++i) {
            std::string labelPath = chip.path + "/temp" + std::to_string(i) + "_label";
            std::string inputPath = chip.path + "/temp" + std::to_string(i) + "_input";
            std::string rawInput = ReadTrimmed(inputPath);
            if (rawInput.empty()) continue;
            std::string label = ReadTrimmed(labelPath);
            if (label.empty()) label = chip.name;
            double temp = std::strtod(rawInput.c_str(), nullptr) / 1000.0;
            ThermalSensor s;
            s.label = label;
            s.tempC = (float)temp;
            s.available = true;
            sensors.push_back(s);
        }
    }

    // Also fold in ACPI thermal_zone entries hwmon doesn't expose (some
    // laptops/VMs only report a subset of sensors this way) — completeness
    // net for the expert Thermals sensor list, not the primary source.
    if (DIR* d = opendir("/sys/class/thermal")) {
        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr) {
            std::string name = ent->d_name;
            if (name.rfind("thermal_zone", 0) != 0) continue;
            std::string base = "/sys/class/thermal/" + name;
            std::string raw = ReadTrimmed(base + "/temp");
            if (raw.empty()) continue;
            double v = std::strtod(raw.c_str(), nullptr) / 1000.0;
            if (v <= 0.0 || v > 150.0) continue; // sanity bound, skip bogus readings

            std::string type = ReadTrimmed(base + "/type");
            std::string label = !type.empty() ? type : name;
            bool duplicate = false;
            for (const ThermalSensor& existing : sensors) {
                if (existing.label == label) { duplicate = true; break; }
            }
            if (duplicate) continue;

            ThermalSensor s;
            s.label = label;
            s.tempC = (float)v;
            s.available = true;
            sensors.push_back(s);
        }
        closedir(d);
    }

    return sensors;
}

float MaxTempForCpu(const std::string& model) {
    static const std::pair<const char*, float> kKnown[] = {
        {"5900X", 90.0f}, {"5950X", 90.0f}, {"5800X", 90.0f}, {"5600X", 90.0f},
    };
    for (const auto& [needle, maxTemp] : kKnown)
        if (model.find(needle) != std::string::npos) return maxTemp;
    return 95.0f;
}
