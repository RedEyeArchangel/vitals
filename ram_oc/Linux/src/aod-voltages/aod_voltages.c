// SPDX-License-Identifier: GPL-2.0
/*
 * aod_voltages.c — AMD AOD (Overclocking Data) memory voltage reader
 *
 * Locates the AMD AOD SystemMemory OperationRegion by parsing ACPI SSDT tables,
 * maps it with memremap, then exposes voltage candidates via sysfs at:
 *   /sys/kernel/aod_voltages/scan       — all float-range values in DSPD
 *   /sys/kernel/aod_voltages/mem_vddio  — MemVddio (VDD)
 *   /sys/kernel/aod_voltages/mem_vddq   — MemVddq
 *   /sys/kernel/aod_voltages/mem_vpp    — MemVpp
 *   /sys/kernel/aod_voltages/full_raw   — entire AOD region (binary; read in chunks)
 *
 * Offsets for the named voltages are set via module parameters after
 * identifying them from the scan output:
 *   modprobe aod_voltages off_vddio=N off_vddq=N off_vpp=N
 * On Family 19h APUs (e.g. Ryzen 8700G Hawk Point) also use:
 *   modprobe aod_voltages family_variant=hawk_point
 *
 * Scan lists u32 values that look like millivolts. Tune the mV window with:
 *   modprobe aod_voltages scan_mv_min=400 scan_mv_max=3500
 * (defaults: 500–3000; widen max for boards with high DRAM voltage mode.)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/memremap.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/kernel.h>   /* ARRAY_SIZE */
#include <asm/processor.h>  /* boot_cpu_data */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TuxTimings");
MODULE_DESCRIPTION("AMD AOD memory voltage reader");
MODULE_VERSION("0.5");

/*
 * Some vendors/AGESA revisions ship the AOD SSDT under different OEM Table IDs.
 * ZenStates-Core recognizes at least:
 *   - "AOD     "
 *   - "AMD AOD"
 *   - "CB-01   " (Lenovo)
 *
 * We do not hard-filter by OEM Table ID anymore; we scan SSDTs for the AOD
 * OperationRegion signatures directly (AODE/AODT).
 */

/*
 * Layout of the AODE OperationRegion (from SSDT9 Field definition):
 *   OUTB  1568 bits = 196 bytes  (offset    0)  — SMI output buffer
 *   AQVS    32 bits =   4 bytes  (offset  196)
 *   SCMI    32 bits =   4 bytes  (offset  200)
 *   SCMD    32 bits =   4 bytes  (offset  204)
 *   DSPD 68128 bits = 8516 bytes (offset  208)  — XMP/timing profiles
 *   RESV    96 bits =  12 bytes  (offset 8724)
 *   RMPD  1120 bits = 140 bytes  (offset 8736)
 *   WCNS  4096 bits = 512 bytes  (offset 8876)  — OC settings/voltages
 *   ...
 *
 * ZenStates-Core Granite Ridge AOD offsets are absolute from AODE start:
 *   MemVddio = 9084  (WCNS + 208)
 *   MemVddq  = 9088  (WCNS + 212)
 *   MemVpp   = 9092  (WCNS + 216)
 */
#define AOD_REGION_SIZE  0x24BB
#define WCNS_OFFSET      8876
#define WCNS_SIZE        512
/* Scan the full region to catch data in any field */
#define SCAN_START       4      /* skip first 4 bytes (status/version) */
#define SCAN_END         AOD_REGION_SIZE

/*
 * Voltages are stored as unsigned 32-bit integers in millivolts.
 * e.g. 1550 mV = 0x0000060E, 1800 mV = 0x00000708
 * Default filter range for scan; override at runtime with scan_mv_min / scan_mv_max.
 */
#define MV_SCAN_DEFAULT_MIN  500U
#define MV_SCAN_DEFAULT_MAX  3000U

/*
 * AML byte patterns for:
 *   OpRegion (AODE, SystemMemory, ...)
 *   OpRegion (AODT, SystemMemory, ...)
 *
 *   5B 80        — DefOpRegion opcode
 *   41 4F 44 45  — NameSeg 'AODE' (or 'AODT')
 *   00           — RegionSpace = SystemMemory
 */
static const u8 aode_pattern[] = { 0x5B, 0x80, 0x41, 0x4F, 0x44, 0x45, 0x00 };
static const u8 aodt_pattern[] = { 0x5B, 0x80, 0x41, 0x4F, 0x44, 0x54, 0x00 };

static void *aod_base;          /* remapped AOD region          */
static struct kobject *aod_kobj;

/* Scan window (millivolts): which u32 values count as voltage candidates in `scan`. */
static uint scan_mv_min = MV_SCAN_DEFAULT_MIN;
static uint scan_mv_max = MV_SCAN_DEFAULT_MAX;

module_param(scan_mv_min, uint, 0400);
MODULE_PARM_DESC(scan_mv_min,
    "Minimum mV for sysfs scan (default 500; lower e.g. 300 to experiment)");
module_param(scan_mv_max, uint, 0400);
MODULE_PARM_DESC(scan_mv_max,
    "Maximum mV for sysfs scan (default 3000; raise for exotic OC / BIOS ranges)");

/* Module parameters: byte offsets of each voltage in the AODE region.
 * Defaults are family-specific (auto-detected from /proc/cpuinfo at load).
 * Override if scan shows different offsets on your board. */
static int off_vddio;
static int off_vddq;
static int off_vpp;
static int off_cpu_vddio;

module_param(off_vddio, int, 0400);
MODULE_PARM_DESC(off_vddio,     "Byte offset of MemVddio (VDD) in AOD region");
module_param(off_vddq, int, 0400);
MODULE_PARM_DESC(off_vddq,      "Byte offset of MemVddq in AOD region");
module_param(off_vpp, int, 0400);
MODULE_PARM_DESC(off_vpp,       "Byte offset of MemVpp in AOD region");
module_param(off_cpu_vddio, int, 0400);
MODULE_PARM_DESC(off_cpu_vddio, "Byte offset of CPU VDDIO in AOD region (ApuVddio)");

/* Read a u32 millivolt value from the AOD region at byte offset. */
static u32 read_mv(int offset)
{
    u32 v;

    /* offset + 4 > AOD_REGION_SIZE would wrap for offset near INT_MAX,
     * bypassing the check; compare via subtraction instead so it can't. */
    if (!aod_base || offset < 0 || offset > AOD_REGION_SIZE - 4)
        return 0;
    memcpy(&v, (u8 *)aod_base + offset, 4);
    return v;
}

/* sysfs: scan — list all millivolt-range integers in the AOD region with their offsets */
static ssize_t scan_show(struct kobject *kobj,
                         struct kobj_attribute *attr, char *buf)
{
    ssize_t len = 0;
    int     i;

    if (!aod_base)
        return scnprintf(buf, PAGE_SIZE, "error: AOD region not mapped\n");

    len += scnprintf(buf + len, PAGE_SIZE - len,
                     "offset  hex     field  value\n"
                     "------  ------  -----  -------\n");

    for (i = SCAN_START; i < SCAN_END - 4; i += 4) {
        u32 mv = read_mv(i);

        if (mv < scan_mv_min || mv > scan_mv_max)
            continue;

        const char *field =
            (i <  196) ? "OUTB" :
            (i <  208) ? "CTRL" :
            (i < 8724) ? "DSPD" :
            (i < 8736) ? "RESV" :
            (i < 8876) ? "RMPD" :
            (i < 9388) ? "WCNS" : "TAIL";
        len += scnprintf(buf + len, PAGE_SIZE - len,
                         "%6d  0x%04X  %-4s  %u mV (%u.%03u V)\n",
                         i, i, field, mv, mv / 1000, mv % 1000);

        if (len >= PAGE_SIZE - 64)
            break;
    }

    if (len <= 32) /* only header printed */
        len += scnprintf(buf + len, PAGE_SIZE - len,
                         "(no voltage-range values found)\n");
    return len;
}

static ssize_t show_named(struct kobject *kobj,
                           struct kobj_attribute *attr,
                           char *buf, int offset)
{
    u32 mv;

    if (!aod_base)
        return scnprintf(buf, PAGE_SIZE, "error: not mapped\n");
    if (offset < 0 || offset > AOD_REGION_SIZE - 4)
        return scnprintf(buf, PAGE_SIZE,
                         "unset — reload with: modprobe aod_voltages off_vddio=N ...\n");

    mv = read_mv(offset);
    return scnprintf(buf, PAGE_SIZE, "%u mV (%u.%03u V)\n",
                     mv, mv / 1000, mv % 1000);
}

static ssize_t mem_vddio_show(struct kobject *k, struct kobj_attribute *a, char *b)
{ return show_named(k, a, b, off_vddio); }

static ssize_t mem_vddq_show(struct kobject *k, struct kobj_attribute *a, char *b)
{ return show_named(k, a, b, off_vddq); }

static ssize_t mem_vpp_show(struct kobject *k, struct kobj_attribute *a, char *b)
{ return show_named(k, a, b, off_vpp); }

static ssize_t cpu_vddio_show(struct kobject *k, struct kobj_attribute *a, char *b)
{ return show_named(k, a, b, off_cpu_vddio); }

/*
 * full_raw — binary read of entire mapped AOD region (supports partial reads / seek).
 * Example:  hexdump -C /sys/kernel/aod_voltages/full_raw
 */
static ssize_t full_raw_read(struct file *filp, struct kobject *kobj,
                             const struct bin_attribute *attr, char *buf,
                             loff_t off, size_t count)
{
    if (!aod_base)
        return -ENODEV;
    if (off >= AOD_REGION_SIZE)
        return 0;
    if (count > AOD_REGION_SIZE - off)
        count = AOD_REGION_SIZE - off;
    memcpy(buf, (u8 *)aod_base + off, count);
    return count;
}

static struct bin_attribute bin_attr_full_raw = {
    .attr = { .name = "full_raw", .mode = 0400 },
    .size = AOD_REGION_SIZE,
    .read = full_raw_read,
};

/* Root-only (0400), not __ATTR_RO's default 0444: full_raw is a dump of the
 * ACPI AOD region, and the named values are read straight from it — no
 * reason for unprivileged local users to see either. vitals already needs
 * root for every other AMD SMU/DRAM value in this same telemetry path
 * (ryzen_smu_drv's sysfs is root-only too), so this costs nothing there. */
static struct kobj_attribute scan_attr      = __ATTR(scan, 0400, scan_show, NULL);
static struct kobj_attribute vddio_attr     = __ATTR(mem_vddio, 0400, mem_vddio_show, NULL);
static struct kobj_attribute vddq_attr      = __ATTR(mem_vddq, 0400, mem_vddq_show, NULL);
static struct kobj_attribute vpp_attr       = __ATTR(mem_vpp, 0400, mem_vpp_show, NULL);
static struct kobj_attribute cpu_vddio_attr = __ATTR(cpu_vddio, 0400, cpu_vddio_show, NULL);

static struct attribute *aod_attrs[] = {
    &scan_attr.attr,
    &vddio_attr.attr,
    &vddq_attr.attr,
    &vpp_attr.attr,
    &cpu_vddio_attr.attr,
    NULL,
};
static struct attribute_group aod_attr_group = { .attrs = aod_attrs };

/*
 * Scan SSDT tables for an AOD OperationRegion:
 *   - AODE (common)
 *   - AODT (seen on some platforms, matching ZenStates-Core behavior)
 *
 * We look specifically for SystemMemory OperationRegions and only handle
 * constant addresses (DWordConst/QWordConst).
 */
static phys_addr_t find_aod_phys(void)
{
    struct acpi_table_header *hdr;
    acpi_status status;
    u32 idx;

    for (idx = 1; ; idx++) {
        u8  *aml;
        u32  aml_len, i;

        status = acpi_get_table("SSDT", idx, &hdr);
        if (ACPI_FAILURE(status))
            break;

        /* hdr->length is firmware-supplied; reject anything too short to
         * even hold the header before subtracting, so a malformed/short
         * SSDT can't underflow aml_len into a ~4GB "length". */
        if (hdr->length <= sizeof(*hdr)) {
            acpi_put_table(hdr);
            continue;
        }

        aml     = (u8 *)hdr + sizeof(*hdr);
        aml_len = hdr->length - (u32)sizeof(*hdr);

        for (i = 0; i + sizeof(aode_pattern) + 10 < aml_len; i++) {
            phys_addr_t addr = 0;
            u8 enc;

            if (memcmp(aml + i, aode_pattern, sizeof(aode_pattern)) != 0 &&
                memcmp(aml + i, aodt_pattern, sizeof(aodt_pattern)) != 0)
                continue;

            enc = aml[i + sizeof(aode_pattern)];

            if (enc == 0x0C) {
                /* DWordConst: 4-byte LE address */
                u32 tmp32;
                memcpy(&tmp32, aml + i + sizeof(aode_pattern) + 1, 4);
                addr = tmp32;
            } else if (enc == 0x0E) {
                /* QWordConst: 8-byte LE address */
                memcpy(&addr, aml + i + sizeof(aode_pattern) + 1, 8);
            } else {
                continue;
            }

            pr_info("aod_voltages: AOD region phys=0x%llx size=0x%x\n",
                    (unsigned long long)addr, AOD_REGION_SIZE);
            acpi_put_table(hdr);
            return addr;
        }

        acpi_put_table(hdr);
    }

    return 0;
}

/* CPU family (same value /proc/cpuinfo's "cpu family" line prints, straight
 * from the struct the kernel already parsed at boot — no VFS/file I/O
 * needed from kernel space for this, unlike the original TuxTimings
 * implementation which opened and sscanf'd /proc/cpuinfo directly). */
static int get_cpu_family(void)
{
    return boot_cpu_data.x86;
}

/*
 * AOD offset table indexed by family + variant.
 * family=0 = any family (wildcard), variant=NULL = any (wildcard).
 * First match wins — put specific entries before generic ones.
 */
/*
 * AOD offset table keyed by SMU codename (from /sys/kernel/ryzen_smu_drv/codename).
 * Codename values: 23 = Granite Ridge, 24 = Hawk Point, etc.
 * Family fallback for older SMU drivers that don't export codename.
 */
static const struct {
    int codename;      /* SMU codename index, or 0 for family-based fallback */
    int family;        /* CPU family fallback, or 0 for codename-only */
    int vddio, vddq, vpp, cpu_vddio;
} aod_offset_table[] = {
    { 24, 0, 9116, 9120, 9124, 8956 },  /* Hawk Point */
    { 23, 0, 9084, 9088, 9092, 9096 },  /* Granite Ridge */
    { 20, 0, 9096, 9100, 9104, 9108 },  /* Raphael */
    {  0, 0x1A, 9084, 9088, 9092, 9096 }, /* Zen 5 (fallback) */
    {  0, 0x19, 9084, 9088, 9092, 9096 }, /* Zen 3/4 (fallback) */
};

/* Read SMU codename index from sysfs (ryzen_smu driver exposes this).
 * Returns -1 if unreadable. */
static int read_smu_codename(void)
{
    struct file *f;
    char buf[16] = {0};
    loff_t pos = 0;

    f = filp_open("/sys/kernel/ryzen_smu_drv/codename", O_RDONLY, 0);
    if (IS_ERR(f))
        return -1;
    kernel_read(f, buf, sizeof(buf) - 1, &pos);
    filp_close(f, NULL);
    return simple_strtol(buf, NULL, 10);
}

static void apply_default_offsets(int family)
{
    if (off_vddio)     /* module param already set — skip auto-detect */
        return;

    int codename = read_smu_codename();

    for (size_t i = 0; i < ARRAY_SIZE(aod_offset_table); i++) {
        const typeof(aod_offset_table[0]) *e = &aod_offset_table[i];
        int match = (e->codename > 0 && e->codename == codename) ||
                    (e->codename == 0 && e->family > 0 && e->family == family);
        if (match) {
            off_vddio = e->vddio;
            off_vddq  = e->vddq;
            off_vpp   = e->vpp;
            off_cpu_vddio = e->cpu_vddio;
            return;
        }
    }
}

static int __init aod_voltages_init(void)
{
    int family = get_cpu_family();
    apply_default_offsets(family);

    phys_addr_t phys = find_aod_phys();

    if (!phys) {
        pr_err("aod_voltages: AOD ACPI region (AODE/AODT) not found\n");
        return -ENODEV;
    }

    aod_base = memremap(phys, AOD_REGION_SIZE, MEMREMAP_WB);
    if (!aod_base) {
        pr_err("aod_voltages: memremap(0x%llx) failed\n",
               (unsigned long long)phys);
        return -ENOMEM;
    }

    aod_kobj = kobject_create_and_add("aod_voltages", kernel_kobj);
    if (!aod_kobj) {
        memunmap(aod_base);
        return -ENOMEM;
    }

    if (sysfs_create_group(aod_kobj, &aod_attr_group) != 0) {
        kobject_put(aod_kobj);
        memunmap(aod_base);
        return -ENOMEM;
    }

    if (sysfs_create_bin_file(aod_kobj, &bin_attr_full_raw) != 0) {
        sysfs_remove_group(aod_kobj, &aod_attr_group);
        kobject_put(aod_kobj);
        memunmap(aod_base);
        return -ENOMEM;
    }

    pr_info("aod_voltages: ready — family=0x%x offsets vddio=%d vddq=%d vpp=%d cpu_vddio=%d scan_mv=%u-%u\n",
            family, off_vddio, off_vddq, off_vpp, off_cpu_vddio, scan_mv_min, scan_mv_max);

    return 0;
}

static void __exit aod_voltages_exit(void)
{
    if (aod_kobj) {
        sysfs_remove_bin_file(aod_kobj, &bin_attr_full_raw);
        sysfs_remove_group(aod_kobj, &aod_attr_group);
        kobject_put(aod_kobj);
    }
    if (aod_base)
        memunmap(aod_base);

    pr_info("aod_voltages: unloaded\n");
}

module_init(aod_voltages_init);
module_exit(aod_voltages_exit);
