#include "pm_table.h"
#include <string.h>
#include <math.h>

/* Named PM table index entry */
typedef struct { int index; int field_offset; } pm_entry_t;

/* Field offsets into smu_metrics_t for named entries */
enum {
    F_FCLK, F_UCLK, F_MCLK, F_VSOC, F_VDDP, F_VDDG_IOD, F_VDDG_CCD,
    F_VDD_MISC, F_VCORE, F_IOD_HOTSPOT
};

typedef struct {
    pm_entry_t named[12];
    int named_count;
    int vid_idx, ppt_idx, socket_power_idx;
    int core_voltage_start, core_temp_start, core_clock_start;
    int max_cores;
} pm_family_map_t;

/* Granite Ridge (default/fallback) */
static const pm_family_map_t GRANITE_RIDGE = {
    .named = {
        {11, F_IOD_HOTSPOT}, {58, F_VDD_MISC}, {71, F_FCLK}, {75, F_UCLK},
        {79, F_MCLK}, {83, F_VSOC}, {259, F_VDDG_IOD}, {261, F_VDDG_CCD},
        {269, F_VDDP}, {271, F_VCORE}
    },
    .named_count = 10,
    .vid_idx = 275, .ppt_idx = 3, .socket_power_idx = 29,
    .core_voltage_start = 309, .core_temp_start = 317, .core_clock_start = 325,
    .max_cores = 8
};

/* Vermeer 0x380804 (5900X/5950X 16-core, older BIOS) */
static const pm_family_map_t VERMEER_380804 = {
    .named = {
        {11, F_IOD_HOTSPOT}, {48, F_FCLK}, {50, F_UCLK}, {51, F_MCLK},
        {44, F_VSOC}, {137, F_VDDP}, {138, F_VDDG_IOD}, {139, F_VDDG_CCD},
        {40, F_VCORE}
    },
    .named_count = 9,
    .vid_idx = 10, .ppt_idx = 1, .socket_power_idx = 29,
    .core_voltage_start = 185, .core_temp_start = 201, .core_clock_start = -1,
    .max_cores = 16
};

/* Vermeer 0x380805 (5950X 16-core, newer BIOS) */
static const pm_family_map_t VERMEER_380805 = {
    .named = {
        {11, F_IOD_HOTSPOT}, {48, F_FCLK}, {50, F_UCLK}, {51, F_MCLK},
        {44, F_VSOC}, {137, F_VDDP}, {138, F_VDDG_IOD}, {139, F_VDDG_CCD},
        {39, F_VCORE}
    },
    .named_count = 9,
    .vid_idx = 10, .ppt_idx = 1, .socket_power_idx = 29,
    .core_voltage_start = 188, .core_temp_start = 204, .core_clock_start = -1,
    .max_cores = 16
};

/* Vermeer 0x380904 (5600X 6-core, older BIOS) */
static const pm_family_map_t VERMEER_380904 = {
    .named = {
        {11, F_IOD_HOTSPOT}, {48, F_FCLK}, {50, F_UCLK}, {51, F_MCLK},
        {44, F_VSOC}, {137, F_VDDP}, {138, F_VDDG_IOD}, {139, F_VDDG_CCD},
        {40, F_VCORE}
    },
    .named_count = 9,
    .vid_idx = 10, .ppt_idx = 1, .socket_power_idx = 29,
    .core_voltage_start = 177, .core_temp_start = 183, .core_clock_start = -1,
    .max_cores = 6
};

/* Vermeer 0x380905 (5600X 6-core, newer BIOS) */
static const pm_family_map_t VERMEER_380905 = {
    .named = {
        {11, F_IOD_HOTSPOT}, {48, F_FCLK}, {50, F_UCLK}, {51, F_MCLK},
        {44, F_VSOC}, {137, F_VDDP}, {138, F_VDDG_IOD}, {139, F_VDDG_CCD},
        {39, F_VCORE}
    },
    .named_count = 9,
    .vid_idx = 10, .ppt_idx = 1, .socket_power_idx = 29,
    .core_voltage_start = 180, .core_temp_start = 186, .core_clock_start = -1,
    .max_cores = 6
};

/* Cezanne 0x400005 (5700G APU) */
static const pm_family_map_t CEZANNE_400005 = {
    .named = {
        {29, F_IOD_HOTSPOT}, {409, F_FCLK}, {410, F_UCLK}, {411, F_MCLK},
        {102, F_VSOC}, {565, F_VDDP}, {98, F_VCORE}
    },
    .named_count = 7,
    .vid_idx = 28, .ppt_idx = 5, .socket_power_idx = 38,
    .core_voltage_start = 208, .core_temp_start = 216, .core_clock_start = -1,
    .max_cores = 8
};

/* Raphael 0x540104 (7800X3D 8-core, PM table 0x00540104) */
static const pm_family_map_t RAPHAEL_540104 = {
    .named = {
        {11, F_IOD_HOTSPOT}, {70, F_FCLK}, {78, F_UCLK}, {74, F_MCLK},
        {82, F_VSOC}, {259, F_VDDG_IOD}, {261, F_VDDG_CCD},
        {269, F_VDDP}, {271, F_VCORE}
    },
    .named_count = 9,
    .vid_idx = 275, .ppt_idx = 3, .socket_power_idx = 29,
    .core_voltage_start = 301, .core_temp_start = 309, .core_clock_start = 317,
    .max_cores = 8
};

/* Raphael 0x540004 (7950X/7900X 16-core, PM table 0x00540004) */
static const pm_family_map_t RAPHAEL_540004 = {
    .named = {
        {11, F_IOD_HOTSPOT}, {56, F_VDD_MISC}, {70, F_FCLK}, {74, F_UCLK},
        {78, F_MCLK}, {82, F_VSOC}, {259, F_VDDG_IOD},
        {261, F_VDDG_CCD}, {268, F_VDDP}, {271, F_VCORE}
    },
    .named_count = 10,
    .vid_idx = 275, .ppt_idx = 3, .socket_power_idx = 29,
    .core_voltage_start = 309, .core_temp_start = 325, .core_clock_start = 341,
    .max_cores = 16
};

/* Matisse 0x240903 (3700X/3800X 8-core) */
static const pm_family_map_t MATISSE_240903 = {
    .named = {
        {11, F_IOD_HOTSPOT}, {48, F_FCLK}, {50, F_UCLK}, {51, F_MCLK},
        {44, F_VSOC}, {125, F_VDDP}, {126, F_VDDG_IOD}, {39, F_VCORE}
    },
    .named_count = 8,
    .vid_idx = 10, .ppt_idx = 1, .socket_power_idx = 29,
    .core_voltage_start = 155, .core_temp_start = 163, .core_clock_start = -1,
    .max_cores = 8
};

/* Matisse 0x240803 (3950X 16-core) */
static const pm_family_map_t MATISSE_240803 = {
    .named = {
        {11, F_IOD_HOTSPOT}, {48, F_FCLK}, {50, F_UCLK}, {51, F_MCLK},
        {44, F_VSOC}, {125, F_VDDP}, {126, F_VDDG_IOD}, {40, F_VCORE}
    },
    .named_count = 8,
    .vid_idx = 10, .ppt_idx = 1, .socket_power_idx = 29,
    .core_voltage_start = 163, .core_temp_start = 179, .core_clock_start = -1,
    .max_cores = 16
};

/* Renoir 0x370003 (4800U APU) */
static const pm_family_map_t RENOIR_370003 = {
    .named = {
        {29, F_IOD_HOTSPOT}, {371, F_FCLK}, {372, F_UCLK}, {373, F_MCLK},
        {101, F_VSOC}, {527, F_VDDP}, {97, F_VCORE}
    },
    .named_count = 7,
    .vid_idx = 28, .ppt_idx = 5, .socket_power_idx = 38,
    .core_voltage_start = 200, .core_temp_start = 208, .core_clock_start = -1,
    .max_cores = 8
};

/* Renoir 0x370005 (Renoir v2 APU) */
static const pm_family_map_t RENOIR_370005 = {
    .named = {
        {29, F_IOD_HOTSPOT}, {378, F_FCLK}, {379, F_UCLK}, {380, F_MCLK},
        {101, F_VSOC}, {534, F_VDDP}, {97, F_VCORE}
    },
    .named_count = 7,
    .vid_idx = 28, .ppt_idx = 5, .socket_power_idx = 38,
    .core_voltage_start = 207, .core_temp_start = 215, .core_clock_start = -1,
    .max_cores = 8
};

/* Granite Ridge 0x620205 (Ryzen 9000, e.g. 9950X) */
static const pm_family_map_t GRANITE_620205 = {
    .named = {
        {11, F_IOD_HOTSPOT}, {58, F_VDD_MISC}, {71, F_FCLK}, {75, F_UCLK},
        {79, F_MCLK}, {83, F_VSOC}, {259, F_VDDG_IOD}, {261, F_VDDG_CCD},
        {269, F_VDDP}, {271, F_VCORE}
    },
    .named_count = 10,
    .vid_idx = 275, .ppt_idx = 3, .socket_power_idx = 220,
    .core_voltage_start = 309, .core_temp_start = 333, .core_clock_start = 349,
    .max_cores = 16
};

/* Granite Ridge 0x620105 (Ryzen 9000 X3D, e.g. 9850X3D) */
static const pm_family_map_t GRANITE_620105 = {
    .named = {
        {11, F_IOD_HOTSPOT}, {58, F_VDD_MISC}, {71, F_FCLK}, {75, F_UCLK},
        {79, F_MCLK}, {83, F_VSOC}, {259, F_VDDG_IOD}, {261, F_VDDG_CCD},
        {269, F_VDDP}, {271, F_VCORE}
    },
    .named_count = 10,
    .vid_idx = 275, .ppt_idx = 3, .socket_power_idx = 220,
    .core_voltage_start = 301, .core_temp_start = 317, .core_clock_start = 325,
    .max_cores = 8
};

/* Raven Ridge 0x1E0004 (2500U APU) */
static const pm_family_map_t RAVEN_1E0004 = {
    .named = {
        {11, F_IOD_HOTSPOT}, {166, F_FCLK}, {167, F_UCLK}, {168, F_MCLK},
        {65, F_VSOC}, {60, F_VDDP}, {61, F_VCORE}
    },
    .named_count = 7,
    .vid_idx = 57, .ppt_idx = 5, .socket_power_idx = 38,
    .core_voltage_start = 104, .core_temp_start = 108, .core_clock_start = -1,
    .max_cores = 4
};

/* Hawk Point 0x4C0009 (Ryzen 8700G, 8-core Phoenix APU) */
static const pm_family_map_t HAWK_POINT_4C0009 = {
    .named = {
        {89, F_FCLK}, {93, F_UCLK}, {97, F_MCLK},
        {101, F_VSOC}, {477, F_VDDP}
    },
    .named_count = 5,
    .vid_idx = 28, .ppt_idx = 3, .socket_power_idx = 38,
    .core_voltage_start = -1, .core_temp_start = -1, .core_clock_start = -1,
    .max_cores = 8
};

static const pm_family_map_t *get_family_map(uint32_t version)
{
    switch (version) {
    case 0x620205: return &GRANITE_620205;
    case 0x620105: return &GRANITE_620105;
    case 0x540004: return &RAPHAEL_540004;
    case 0x540104: return &RAPHAEL_540104;
    case 0x380804: return &VERMEER_380804;
    case 0x380805: return &VERMEER_380805;
    case 0x380904: return &VERMEER_380904;
    case 0x380905: return &VERMEER_380905;
    case 0x400005: return &CEZANNE_400005;
    case 0x240903: return &MATISSE_240903;
    case 0x240803: return &MATISSE_240803;
    case 0x370003: return &RENOIR_370003;
    case 0x370005: return &RENOIR_370005;
    case 0x4C0009: return &HAWK_POINT_4C0009;
    case 0x1E0004: return &RAVEN_1E0004;
    default:       return &GRANITE_RIDGE;
    }
}

static inline float safe_get(const float *t, int count, int idx)
{
    return (idx >= 0 && idx < count) ? t[idx] : 0.0f;
}

static void apply_named(const pm_family_map_t *map, const float *t, int count, smu_metrics_t *m)
{
    for (int i = 0; i < map->named_count; i++) {
        float v = safe_get(t, count, map->named[i].index);
        switch (map->named[i].field_offset) {
        case F_FCLK:       m->fclk_mhz = v; break;
        case F_UCLK:       m->uclk_mhz = v; break;
        case F_MCLK:       m->mclk_mhz = v; break;
        case F_VSOC:       m->vsoc = v; break;
        case F_VDDP:       m->vddp = v; break;
        case F_VDDG_IOD:   m->vddg_iod = v; break;
        case F_VDDG_CCD:   m->vddg_ccd = v; break;
        case F_VDD_MISC:   m->vdd_misc = v; break;
        case F_VCORE:      m->vcore = v; break;
        case F_IOD_HOTSPOT: {
            if (v >= 1.0f && v <= 150.0f) {
                m->iod_hotspot_c = v;
                m->has_iod_hotspot = true;
            }
            break;
        }
        }
    }
}

static float try_plausible_temp(const float *t, int count)
{
    int candidates[] = {1, 448, 449};
    for (int i = 0; i < 3; i++) {
        float v = safe_get(t, count, candidates[i]);
        if (v >= 1.0f && v <= 150.0f) return v;
    }
    return 0.0f;
}

/* Derive aggregate core clock in MHz from per-core clocks in GHz */
static void compute_core_clock_from_clocks(smu_metrics_t *m)
{
    if (m->core_clocks_count <= 0) return;

    float max_ghz = 0.0f;
    for (int i = 0; i < m->core_clocks_count; i++) {
        float ghz = m->core_clocks_ghz[i];
        if (ghz > max_ghz)
            max_ghz = ghz;
    }
    if (max_ghz >= 0.5f && max_ghz <= 6.5f)
        m->core_clock_mhz = max_ghz * 1000.0f;
}

void pm_table_read(uint32_t version, const float *table, int count,
                   int physical_cores, smu_metrics_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!table || count < 4) return;

    /* All families use the version-based map for named entries */
    const pm_family_map_t *map = get_family_map(version);
    apply_named(map, table, count, out);

    /* Per-core temps and voltages — clamp to physical core count. */
    int nc = map->max_cores;
    if (physical_cores > 0 && physical_cores < nc)
        nc = physical_cores;
    if (nc > MAX_CORES) nc = MAX_CORES;

    if (map->core_temp_start + nc <= count) {
        out->core_temps_count = nc;
        for (int i = 0; i < nc; i++)
            out->core_temps_c[i] = table[map->core_temp_start + i];
    }
    if (map->core_voltage_start + nc <= count) {
        int out_idx = 0;
        // Scan all possible slots, collect up to 'nc' non-zero voltages
        for (int i = 0; i < map->max_cores; i++) {
            if (map->core_voltage_start + i >= count) break;
            float v = table[map->core_voltage_start + i];
            if (v != 0.0f && out_idx < nc) {
                out->core_voltages[out_idx++] = v;
                if (out_idx >= nc) break;
            }
        }
        // Fill the rest with zeroes
        for (int i = out_idx; i < nc; i++) {
            out->core_voltages[i] = 0.0f;
        }
        out->core_voltages_count = out_idx;
    }

    /* VID — always read directly from vid_idx (never in named arrays) */
    float vid_v = safe_get(table, count, map->vid_idx);
    if (vid_v > 0) out->vid = vid_v;

    /* PPT — always read directly from ppt_idx */
    float ppt_v = safe_get(table, count, map->ppt_idx);
    if (ppt_v >= 0.5f && ppt_v <= 400.0f) out->ppt_w = ppt_v;

    /* Socket power */
    float sp = safe_get(table, count, map->socket_power_idx);
    if (sp >= 0.5f && sp <= 400.0f) out->package_power_w = sp;

    out->cpu_temp_c = try_plausible_temp(table, count);

    /* Per-core clocks from family map (if available) */
    if (map->core_clock_start >= 0 && map->core_clock_start + nc <= count) {
        out->core_clocks_count = nc;
        for (int i = 0; i < nc; i++)
            out->core_clocks_ghz[i] = table[map->core_clock_start + i];
    }

    /* Derive aggregate core_clock_mhz from per-core clocks */
    compute_core_clock_from_clocks(out);
}
