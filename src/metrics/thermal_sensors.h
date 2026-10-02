#pragma once

#include "metrics.h" // ThermalSensor

#include <set>
#include <string>
#include <vector>

// --- /sys/class/hwmon: the real, universal Linux sensor mechanism (what
// `sensors` from lm-sensors reads). thermal_zone is ACPI-only and often
// empty on desktop boards — plenty of otherwise-fine motherboards expose
// zero thermal_zone entries despite having a perfectly good hwmon chip. ---
struct HwmonChip { std::string name, path; };

std::vector<HwmonChip> EnumerateHwmon();

// First temp*_input under a hwmon chip dir, in Celsius, or -1 if none.
double FirstHwmonTemp(const std::string& chipPath);

double HwmonTempByNames(const std::vector<HwmonChip>& chips, const std::set<std::string>& names);

// First in*_input (millivolts -> volts) across all hwmon chips whose in*_label
// matches `label` case-insensitively, or -1 if none. Not scoped to a chip name
// set: labels like "DRAM" (as exposed by e.g. the out-of-tree nct6687 driver
// for boards with an NCT6687D Super I/O) are distinctive enough on their own,
// and this keeps it working across whichever Super I/O driver a board needs.
double HwmonVoltageByLabel(const std::vector<HwmonChip>& chips, const std::string& label);

// Fallback for machines with no matching hwmon chip at all: whatever
// thermal_zone offers (some laptops/VMs only expose ACPI zones).
double ReadFallbackThermalZoneTemp();

// Every temp sensor the machine exposes (hwmon + ACPI thermal_zone) — for
// the Performance > Thermals expert sensor list, not the primary CPU/GPU/
// board readings above (those stay curated via HwmonTempByNames).
std::vector<ThermalSensor> ReadThermalSensors();

// Tjmax by CPU model substring, from AMD/Intel public datasheets. Covers
// common desktop parts; unlisted CPUs fall back to a generic 95 C ceiling.
float MaxTempForCpu(const std::string& model);
