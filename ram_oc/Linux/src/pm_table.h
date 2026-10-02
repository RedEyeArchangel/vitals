#ifndef PM_TABLE_H
#define PM_TABLE_H

#include "types.h"

/* Read PM table binary and apply family-specific index mappings to fill metrics.
 * version:         PM table version from /sys/kernel/ryzen_smu_drv/pm_table_version
 * table:          array of floats from pm_table binary
 * count:          number of floats
 * physical_cores: number of physical cores (from topology); used to clamp per-core
 *                 reads so 12-core/6-core parts sharing tables with 16-core variants
 *                 don't read out-of-bounds PM table entries.
 */
void pm_table_read(uint32_t version, const float *table, int count,
                   int physical_cores, smu_metrics_t *out);

#endif
