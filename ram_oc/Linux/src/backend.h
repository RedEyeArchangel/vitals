#ifndef BACKEND_H
#define BACKEND_H

#include "types.h"

/* Returns true if the ryzen_smu driver is loaded and accessible — gates the
 * AMD-only fields (voltages, PPT, FCLK/UCLK/MCLK, JEDEC sub-timings). */
int backend_is_supported(void);

/* Read all system data into out. Call every ~1 second for live refresh.
 * Static data (dmidecode, AGESA) is cached after first call. Always fills
 * the vendor-neutral fields (DIMM/SPD info, memory speed/type, per-core
 * temp/usage/freq, package power via RAPL) regardless of CPU vendor;
 * out->smu_supported reports whether the AMD-only fields above are also
 * populated. Safe to call unconditionally — never gate this call itself
 * on backend_is_supported(). */
void backend_read_summary(system_summary_t *out);

/* Unload any kernel modules that were loaded by backend_read_summary().
 * Call once on application exit. */
void backend_cleanup(void);

/* Returns a malloc'd string with a raw PM table + AOD sysfs debug dump.
 * Caller must free(). Returns NULL on failure. */
char *backend_read_debug_dump(void);

#endif
