// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "helper/time_support.h"
#include "helper/list.h"
#include "helper/log.h"

#include "arm_adi_v5.h"
#include "arm_coresight.h"
#include "target.h"

#include "arm_etmv4.h"

static LIST_HEAD(all_etmv4);

/* Dirty flags for staged HW configuration changes. */
struct etmv4_config_options {
    bool traceid_set;
    bool config_set;    /* any of the trace configuration options changed */
};

/* Capabilities decoded from the TRCIDR registers at init. */
struct etmv4_caps {
    /* TRCIDR1 */
    unsigned int arch_minor;    /* ETMv4.x */
    unsigned int designer;      /* JEP106-style code, 0x41 = Arm */
    unsigned int revision;
    /* TRCIDR0 */
    bool instp0;                /* load/store traced as P0 elements */
    bool data_tracing;          /* TRCDATA != 0 */
    bool branch_broadcast;
    bool cond_tracing;          /* conditional instruction tracing */
    bool cycle_counting;        /* TRCCCI */
    bool retstack;
    unsigned int numevent;      /* events supported, 0..4 */
    unsigned int condtype;
    unsigned int qsupp;
    bool qfilt;
    bool trcexdata;
    bool tsmark;
    bool commopt;
    unsigned int bf;            /* branch future support */
    unsigned int ts_bits;       /* global timestamp size, 0 = none */
    /* TRCIDR2 */
    unsigned int ia_bits;       /* instruction address size */
    unsigned int cid_bits;      /* 0 = no context ID tracing */
    unsigned int vmid_bits;     /* 0 = no VMID tracing */
    unsigned int da_bits;       /* 0 = no data address tracing */
    unsigned int dv_bits;       /* 0 = no data value tracing */
    unsigned int cc_bits;       /* cycle counter width, CCSIZE + 12 */
    unsigned int vmidopt;
    bool wfxmode;
    /* TRCIDR3 */
    unsigned int ccitmin;       /* min TRCCCCTLR.THRESHOLD value */
    unsigned int exlevel_s;     /* per-EL instruction trace support masks */
    unsigned int exlevel_ns;
    bool trcerr;                /* TRCVICTLR.TRCERR supported */
    bool syncpr_fixed;          /* TRCIDR3.SYNCPR: TRCSYNCPR is read-only */
    bool stallctl;
    bool sysstall;
    bool nooverflow;
    unsigned int numproc;       /* PEs traceable by this unit */
    /* TRCIDR4 */
    unsigned int numacpairs;
    unsigned int numdvc;
    bool suppdac;
    unsigned int numpc;
    unsigned int numrspair;     /* 0 on v4.3+ means no resource selectors */
    unsigned int numsscc;
    unsigned int numcidc;
    unsigned int numvmidc;
    /* TRCIDR5 */
    unsigned int numextin;
    unsigned int numextinsel;
    unsigned int traceid_bits;  /* TRACEIDSIZE, 7 on ATB */
    bool atbtrig;
    bool lpoverride;
    unsigned int numseqstate;
    unsigned int numcntr;
    bool redfuncntr;
};

/* Representation of a TRCIDR register with the necessary information */
struct etmv4_idr_reg {
    uint32_t value;
    uint32_t offset;
    const char *name;
};

struct etmv4_object {
    struct list_head lh;
    char *name;
    bool initialised;
    bool trace_requested;   /* persists across resets */
    bool enabled;
    struct etmv4_config_options pending_config;

    struct adiv5_mem_ap_spot spot;
    struct adiv5_ap *ap;

    /* host-cached register values, written by etmv4_commit_config() */
    uint32_t traceid;                   /* TRCTRACEIDR */
    uint32_t syncpr;                    /* TRCSYNCPR */
    union etmv4_trcconfigr configr;     /* TRCCONFIGR */
    union etmv4_trcvictlr victlr;       /* TRCVICTLR */
    uint32_t cc_threshold;              /* TRCCCCTLR; 0 = use TRCIDR3.CCITMIN */

    struct etmv4_idr_reg trcidr[ETMV4_NUM_TRCIDR];  /* raw copy for trace decoders */
    struct etmv4_caps caps;
};

static const struct etmv4_idr_reg etmv4_trcidr_defaults[ETMV4_NUM_TRCIDR] = {
    {.offset = ETMV4_TRCIDR0,  .name = "TRCIDR0"},
    {.offset = ETMV4_TRCIDR1,  .name = "TRCIDR1"},
    {.offset = ETMV4_TRCIDR2,  .name = "TRCIDR2"},
    {.offset = ETMV4_TRCIDR3,  .name = "TRCIDR3"},
    {.offset = ETMV4_TRCIDR4,  .name = "TRCIDR4"},
    {.offset = ETMV4_TRCIDR5,  .name = "TRCIDR5"},
    {.offset = ETMV4_TRCIDR6,  .name = "TRCIDR6"},
    {.offset = ETMV4_TRCIDR7,  .name = "TRCIDR7"},
    {.offset = ETMV4_TRCIDR8,  .name = "TRCIDR8"},
    {.offset = ETMV4_TRCIDR9,  .name = "TRCIDR9"},
    {.offset = ETMV4_TRCIDR10, .name = "TRCIDR10"},
    {.offset = ETMV4_TRCIDR11, .name = "TRCIDR11"},
    {.offset = ETMV4_TRCIDR12, .name = "TRCIDR12"},
    {.offset = ETMV4_TRCIDR13, .name = "TRCIDR13"},
};

/********************************************
 * Helpers for device access
 *******************************************/

static int etmv4_read32(struct etmv4_object *obj, uint32_t offset, uint32_t *val)
{
    return mem_ap_read_atomic_u32(obj->ap, obj->spot.base + offset, val);
}

static int etmv4_write32(struct etmv4_object *obj, uint32_t offset, uint32_t val)
{
    return mem_ap_write_atomic_u32(obj->ap, obj->spot.base + offset, val);
}

static int etmv4_queue_read32(struct etmv4_object *obj, uint32_t offset, uint32_t *val)
{
    return mem_ap_read_u32(obj->ap, obj->spot.base + offset, val);
}

static int etmv4_queue_write32(struct etmv4_object *obj, uint32_t offset, uint32_t val)
{
    return mem_ap_write_u32(obj->ap, obj->spot.base + offset, val);
}

static int etmv4_dap_run(struct etmv4_object *obj)
{
    return dap_run(obj->spot.dap);
}

static int etmv4_poll_bit(struct etmv4_object *obj, uint32_t offset, uint32_t mask,
        uint32_t expected, unsigned int timeout_ms)
{
    int64_t deadline = timeval_ms() + (int64_t)timeout_ms;
    uint32_t val;

    do {
        int retval = etmv4_read32(obj, offset, &val);
        if (retval != ERROR_OK)
            return retval;
        if ((val & mask) == expected)
            return ERROR_OK;
        alive_sleep(1);
    } while (timeval_ms() < deadline);

    return ERROR_TIMEOUT_REACHED;
}

/********************************************
 * Unlock, identity and capabilities
 *******************************************/

static int etmv4_unlock(struct etmv4_object *obj)
{
    uint32_t lsr;
    int retval = etmv4_read32(obj, ARM_CS_LSR, &lsr);
    if (retval != ERROR_OK)
        return retval;

    if (!(lsr & ARM_CS_LSR_SLI)) {
        LOG_DEBUG("ETMv4 %s: lock not enforced on this AP, skipping LAR", obj->name);
        return ERROR_OK;
    }
    return etmv4_write32(obj, ARM_CS_LAR, ARM_CS_LAR_UNLOCK);
}

static int etmv4_validate_identity(struct etmv4_object *obj)
{
    uint32_t devarch;
    int retval = etmv4_read32(obj, ARM_CS_C9_DEVARCH, &devarch);
    if (retval != ERROR_OK) {
        LOG_ERROR("ETMv4 %s: failed to read DEVARCH", obj->name);
        return retval;
    }

    if (!(devarch & ARM_CS_C9_DEVARCH_PRESENT) ||
            (devarch & ARM_CS_C9_DEVARCH_ARCHID_MASK) != ETMV4_DEVARCH_ARCHID) {
        LOG_ERROR("ETMv4 %s: DEVARCH 0x%08" PRIx32 " is not an ETMv4 trace unit",
                obj->name, devarch);
        return ERROR_FAIL;
    }
    return ERROR_OK;
}

/* Extract a multi-bit field given its <field>_MASK and <field>_SHIFT defines. */
#define ETMV4_FIELD(reg, field) (((reg) & field##_MASK) >> field##_SHIFT)

static int etmv4_parse_caps(struct etmv4_object *obj)
{
    struct etmv4_caps *caps = &obj->caps;
    uint32_t idr0 = obj->trcidr[0].value;
    uint32_t idr1 = obj->trcidr[1].value;
    uint32_t idr2 = obj->trcidr[2].value;
    uint32_t idr3 = obj->trcidr[3].value;
    uint32_t idr4 = obj->trcidr[4].value;
    uint32_t idr5 = obj->trcidr[5].value;

    unsigned int archmaj = ETMV4_FIELD(idr1, ETMV4_TRCIDR1_ARCHMAJ);
    caps->arch_minor = ETMV4_FIELD(idr1, ETMV4_TRCIDR1_ARCHMIN);
    if (archmaj != ETMV4_ARCH_MAJOR) {
        LOG_ERROR("ETMv4 %s: unsupported trace architecture %u.%u",
                obj->name, archmaj, caps->arch_minor);
        return ERROR_FAIL;
    }
    caps->designer = ETMV4_FIELD(idr1, ETMV4_TRCIDR1_DESIGNER);
    caps->revision = ETMV4_FIELD(idr1, ETMV4_TRCIDR1_REVISION);

    caps->nooverflow = !!(idr3 & ETMV4_TRCIDR3_NOOVERFLOW);
    caps->sysstall = !!(idr3 & ETMV4_TRCIDR3_SYSSTALL);
    caps->stallctl = !!(idr3 & ETMV4_TRCIDR3_STALLCTL);
    caps->syncpr_fixed = !!(idr3 & ETMV4_TRCIDR3_SYNCPR);
    caps->trcerr = !!(idr3 & ETMV4_TRCIDR3_TRCERR);
    caps->exlevel_ns = ETMV4_FIELD(idr3, ETMV4_TRCIDR3_EXLEVEL_NS);
    caps->exlevel_s = ETMV4_FIELD(idr3, ETMV4_TRCIDR3_EXLEVEL_S);
    caps->numproc = ((ETMV4_FIELD(idr3, ETMV4_TRCIDR3_NUMPROC_HI) << 3)
            | ETMV4_FIELD(idr3, ETMV4_TRCIDR3_NUMPROC_LO)) + 1;
    caps->ccitmin = ETMV4_FIELD(idr3, ETMV4_TRCIDR3_CCITMIN);

    caps->numvmidc = ETMV4_FIELD(idr4, ETMV4_TRCIDR4_NUMVMIDC);
    caps->numcidc = ETMV4_FIELD(idr4, ETMV4_TRCIDR4_NUMCIDC);
    caps->numsscc = ETMV4_FIELD(idr4, ETMV4_TRCIDR4_NUMSSCC);
    caps->numpc = ETMV4_FIELD(idr4, ETMV4_TRCIDR4_NUMPC);
    caps->suppdac = !!(idr4 & ETMV4_TRCIDR4_SUPPDAC);
    caps->numdvc = ETMV4_FIELD(idr4, ETMV4_TRCIDR4_NUMDVC);
    caps->numacpairs = ETMV4_FIELD(idr4, ETMV4_TRCIDR4_NUMACPAIRS);
    /* NUMRSPAIR 0 means one pair, except on ETMv4.3+ where it means none */
    unsigned int numrspair = ETMV4_FIELD(idr4, ETMV4_TRCIDR4_NUMRSPAIR);
    if (caps->arch_minor >= 3 && numrspair == 0)
        caps->numrspair = 0;
    else
        caps->numrspair = numrspair + 1;

    caps->commopt = !!(idr0 & ETMV4_TRCIDR0_COMMOPT);
    caps->ts_bits = ETMV4_FIELD(idr0, ETMV4_TRCIDR0_TSSIZE) * 8;
    caps->tsmark = !!(idr0 & ETMV4_TRCIDR0_TSMARK);
    caps->bf = ETMV4_FIELD(idr0, ETMV4_TRCIDR0_BF);
    caps->trcexdata = !!(idr0 & ETMV4_TRCIDR0_TRCEXDATA);
    caps->qsupp = ETMV4_FIELD(idr0, ETMV4_TRCIDR0_QSUPP);
    caps->qfilt = !!(idr0 & ETMV4_TRCIDR0_QFILT);
    caps->condtype = ETMV4_FIELD(idr0, ETMV4_TRCIDR0_CONDTYPE);
    caps->retstack = !!(idr0 & ETMV4_TRCIDR0_RETSTACK);
    caps->cycle_counting = !!(idr0 & ETMV4_TRCIDR0_TRCCCI);
    caps->cond_tracing = !!(idr0 & ETMV4_TRCIDR0_TRCCOND);
    caps->branch_broadcast = !!(idr0 & ETMV4_TRCIDR0_TRCBB);
    caps->data_tracing = ETMV4_FIELD(idr0, ETMV4_TRCIDR0_TRCDATA) != 0;
    caps->instp0 = ETMV4_FIELD(idr0, ETMV4_TRCIDR0_INSTP0) != 0;
    /* NUMEVENT 0 means one event, except on ETMv4.3+ with no resource pairs */
    if (caps->numrspair == 0)
        caps->numevent = 0;
    else
        caps->numevent = ETMV4_FIELD(idr0, ETMV4_TRCIDR0_NUMEVENT) + 1;

    /* TRCIDR2 size fields are in bytes; CCSIZE is the counter width minus 12 */
    caps->wfxmode = !!(idr2 & ETMV4_TRCIDR2_WFXMODE);
    caps->vmidopt = ETMV4_FIELD(idr2, ETMV4_TRCIDR2_VMIDOPT);
    caps->cc_bits = caps->cycle_counting ?
            ETMV4_FIELD(idr2, ETMV4_TRCIDR2_CCSIZE) + 12 : 0;
    caps->dv_bits = ETMV4_FIELD(idr2, ETMV4_TRCIDR2_DVSIZE) * 8;
    caps->da_bits = ETMV4_FIELD(idr2, ETMV4_TRCIDR2_DASIZE) * 8;
    caps->vmid_bits = ETMV4_FIELD(idr2, ETMV4_TRCIDR2_VMIDSIZE) * 8;
    caps->cid_bits = ETMV4_FIELD(idr2, ETMV4_TRCIDR2_CIDSIZE) * 8;
    caps->ia_bits = ETMV4_FIELD(idr2, ETMV4_TRCIDR2_IASIZE) * 8;

    caps->redfuncntr = !!(idr5 & ETMV4_TRCIDR5_REDFUNCNTR);
    caps->numcntr = ETMV4_FIELD(idr5, ETMV4_TRCIDR5_NUMCNTR);
    caps->numseqstate = ETMV4_FIELD(idr5, ETMV4_TRCIDR5_NUMSEQSTATE);
    caps->lpoverride = !!(idr5 & ETMV4_TRCIDR5_LPOVERRIDE);
    caps->atbtrig = !!(idr5 & ETMV4_TRCIDR5_ATBTRIG);
    caps->traceid_bits = ETMV4_FIELD(idr5, ETMV4_TRCIDR5_TRACEIDSIZE);
    caps->numextinsel = ETMV4_FIELD(idr5, ETMV4_TRCIDR5_NUMEXTINSEL);
    caps->numextin = ETMV4_FIELD(idr5, ETMV4_TRCIDR5_NUMEXTIN);

    return ERROR_OK;
}

static int etmv4_read_capabilities(struct etmv4_object *obj)
{
    int retval;

    for (unsigned int i = 0; i < ETMV4_NUM_TRCIDR; i++) {
        retval = etmv4_queue_read32(obj, obj->trcidr[i].offset,
                &obj->trcidr[i].value);
        if (retval != ERROR_OK)
            return retval;
    }
    retval = etmv4_dap_run(obj);
    if (retval != ERROR_OK) {
        LOG_ERROR("ETMv4 %s: failed to read the TRCIDR registers", obj->name);
        return retval;
    }

    retval = etmv4_parse_caps(obj);
    if (retval != ERROR_OK)
        return retval;

    const struct etmv4_caps *caps = &obj->caps;
    LOG_INFO("ETMv4 %s: ETMv4.%u trace unit", obj->name, caps->arch_minor);
    LOG_DEBUG("ETMv4 %s: designer 0x%x rev %u, %u PE(s), ia %u-bit cid %u-bit vmid %u-bit",
            obj->name, caps->designer, caps->revision, caps->numproc,
            caps->ia_bits, caps->cid_bits, caps->vmid_bits);
    LOG_DEBUG("ETMv4 %s: cyccnt %u-bit (min threshold %u), ts %u-bit, bb %d cond %d "
            "retstack %d data %d stall %d",
            obj->name, caps->cc_bits, caps->ccitmin, caps->ts_bits,
            caps->branch_broadcast, caps->cond_tracing, caps->retstack,
            caps->data_tracing, caps->stallctl);
    LOG_DEBUG("ETMv4 %s: acpair %u cidc %u vmidc %u sscc %u rspair %u cntr %u "
            "seqstate %u extin %u event %u",
            obj->name, caps->numacpairs, caps->numcidc, caps->numvmidc,
            caps->numsscc, caps->numrspair, caps->numcntr, caps->numseqstate,
            caps->numextin, caps->numevent);
    return ERROR_OK;
}

/********************************************
 * Trace unit programming
 *******************************************/

static int etmv4_stop_trace_unit(struct etmv4_object *obj)
{
    int retval = etmv4_write32(obj, ETMV4_TRCPRGCTLR, 0);
    if (retval != ERROR_OK) {
        LOG_ERROR("ETMv4 %s: failed to write TRCPRGCTLR", obj->name);
        return retval;
    }
    retval = etmv4_poll_bit(obj, ETMV4_TRCSTATR, ETMV4_TRCSTATR_IDLE,
            ETMV4_TRCSTATR_IDLE, ETMV4_POLL_TIMEOUT_MS);
    if (retval != ERROR_OK)
        LOG_ERROR("ETMv4 %s: timeout waiting for the trace unit to go idle", obj->name);
    return retval;
}

static int etmv4_start_trace_unit(struct etmv4_object *obj)
{
    int retval = etmv4_write32(obj, ETMV4_TRCPRGCTLR, ETMV4_TRCPRGCTLR_EN);
    if (retval != ERROR_OK) {
        LOG_ERROR("ETMv4 %s: failed to write TRCPRGCTLR", obj->name);
        return retval;
    }
    retval = etmv4_poll_bit(obj, ETMV4_TRCSTATR, ETMV4_TRCSTATR_IDLE, 0,
            ETMV4_POLL_TIMEOUT_MS);
    if (retval != ERROR_OK)
        LOG_ERROR("ETMv4 %s: timeout waiting for the trace unit to leave idle", obj->name);
    return retval;
}

/*
 * Checks of the staged configuration against the trace unit capabilities;
 * only meaningful once the capabilities have been read.
 */
static int etmv4_validate_config(struct etmv4_object *obj)
{
    if (obj->traceid < ETMV4_TRACEID_MIN || obj->traceid > ETMV4_TRACEID_MAX) {
        LOG_ERROR("ETMv4 %s: trace ID %" PRIu32 " outside valid range %u..%u",
                obj->name, obj->traceid, ETMV4_TRACEID_MIN, ETMV4_TRACEID_MAX);
        return ERROR_FAIL;
    }
    if (obj->configr.instp0 && !obj->caps.instp0) {
        LOG_ERROR("ETMv4 %s: Load/Store instruction tracing is not implemented", obj->name);
        return ERROR_FAIL;
    }
    if (obj->configr.bb && !obj->caps.branch_broadcast) {
        LOG_ERROR("ETMv4 %s: branch broadcasting is not implemented", obj->name);
        return ERROR_FAIL;
    }
    if (obj->configr.cci && !obj->caps.cycle_counting) {
        LOG_ERROR("ETMv4 %s: cycle counting is not implemented", obj->name);
        return ERROR_FAIL;
    }
    if (obj->cc_threshold && obj->cc_threshold < obj->caps.ccitmin) {
        LOG_ERROR("ETMv4 %s: cycle count threshold %" PRIu32 " is below the minimum %u",
                obj->name, obj->cc_threshold, obj->caps.ccitmin);
        return ERROR_FAIL;
    }
    if (obj->configr.cid && !obj->caps.cid_bits) {
        LOG_ERROR("ETMv4 %s: context ID tracing is not implemented", obj->name);
        return ERROR_FAIL;
    }
    if (obj->configr.vmid && !obj->caps.vmid_bits) {
        LOG_ERROR("ETMv4 %s: virtual context ID tracing is not implemented", obj->name);
        return ERROR_FAIL;
    }
    if (obj->configr.cond && !obj->caps.cond_tracing) {
        LOG_ERROR("ETMv4 %s: conditional non-branch instruction tracing is not implemented", obj->name);
        return ERROR_FAIL;
    }
    if (obj->configr.ts && !obj->caps.ts_bits) {
        LOG_ERROR("ETMv4 %s: global timestamping is not implemented", obj->name);
        return ERROR_FAIL;
    }
    if (obj->configr.rs && !obj->caps.retstack) {
        LOG_ERROR("ETMv4 %s: the return stack is not implemented", obj->name);
        return ERROR_FAIL;
    }
    if (obj->caps.vmidopt == 0 || obj->caps.vmidopt == 2) {
        /* the bit is RES0 (vmidopt 0) or RES1 (vmidopt 2) */
        if (obj->configr.vmidopt != obj->caps.vmidopt >> 1) {
            LOG_ERROR("ETMv4 %s: Virtual context id selection is not implemented", obj->name);
            return ERROR_FAIL;
        }
    }
    if ((obj->configr.da || obj->configr.dv) && !obj->caps.data_tracing) {
        LOG_ERROR("ETMv4 %s: data tracing is not implemented", obj->name);
        return ERROR_FAIL;
    }

    return ERROR_OK;
}

/*
 * Applies the capability-dependent parts of the cached configuration once the
 * capabilities are known: features that are always enabled when implemented
 * (tracing of system error exceptions), and defaults that must degrade when
 * not implemented (branch broadcasting).
 *
 * Conditional non-branch instruction tracing (TRCCONFIGR.COND) is left
 * disabled even when implemented: there is no -cond-trace staging option to
 * turn it back off, and common decoders (e.g. OpenCSD's ETMv4I decoder)
 * reject any stream with COND != disabled outright (OCSD_ERR_HW_CFG_UNSUPP),
 * so defaulting it on breaks decoding for every consumer with no way out.
 */
static void etmv4_apply_caps_config(struct etmv4_object *obj)
{
    const struct etmv4_caps *caps = &obj->caps;

    obj->victlr.trcerr = caps->trcerr;
    /* TRCCONFIGR.VMIDOPT is RES0/RES1 unless the hardware makes it selectable */
    if (caps->vmidopt != 1)
        obj->configr.vmidopt = caps->vmidopt >> 1;
    if (obj->configr.bb && !caps->branch_broadcast) {
        LOG_DEBUG("ETMv4 %s: branch broadcasting not implemented, ignoring flag", obj->name);
        obj->configr.bb = 0;
    }
    if (obj->configr.rs && !caps->retstack) {
        LOG_DEBUG("ETMv4 %s: return stack not implemented, ignoring flag", obj->name);
        obj->configr.rs = 0;
    }
}

/* Only callable while the trace unit is idle and the capabilities are read. */
static int etmv4_commit_config(struct etmv4_object *obj, bool override)
{
    const struct etmv4_caps *caps = &obj->caps;
    int r;

    if (override || obj->pending_config.traceid_set) {
        r = etmv4_queue_write32(obj, ETMV4_TRCTRACEIDR, obj->traceid);
        if (r != ERROR_OK)
            return r;
        obj->pending_config.traceid_set = false;
    }

    if (override || obj->pending_config.config_set) {
        r = etmv4_queue_write32(obj, ETMV4_TRCCONFIGR, obj->configr.word);
        if (r != ERROR_OK)
            return r;
        if (obj->configr.cci) {
            uint32_t threshold = obj->cc_threshold ? obj->cc_threshold : caps->ccitmin;
            r = etmv4_queue_write32(obj, ETMV4_TRCCCCTLR, threshold);
            if (r != ERROR_OK)
                return r;
        }
        if (obj->configr.ts) {
            /* FALSE event: timestamps only at trace synchronization points */
            r = etmv4_queue_write32(obj, ETMV4_TRCTSCTLR, 0);
            if (r != ERROR_OK)
                return r;
        }
        if (obj->configr.bb && caps->numacpairs) {
            /* exclude mode with no ranges: broadcast in the whole memory map */
            r = etmv4_queue_write32(obj, ETMV4_TRCBBCTLR, 0);
            if (r != ERROR_OK)
                return r;
        }
        if (obj->configr.da || obj->configr.dv) {
            r = etmv4_queue_write32(obj, ETMV4_TRCVDCTLR, ETMV4_EVENT_TRUE);
            if (r != ERROR_OK)
                return r;
            if (caps->numacpairs) {
                r = etmv4_queue_write32(obj, ETMV4_TRCVDSACCTLR, 0);
                if (r != ERROR_OK)
                    return r;
                r = etmv4_queue_write32(obj, ETMV4_TRCVDARCCTLR, 0);
                if (r != ERROR_OK)
                    return r;
            }
        }
        obj->pending_config.config_set = false;
    }

    if (override) {
        r = etmv4_queue_write32(obj, ETMV4_TRCVICTLR, obj->victlr.word);
        if (r != ERROR_OK)
            return r;
        if (!caps->syncpr_fixed) {
            r = etmv4_queue_write32(obj, ETMV4_TRCSYNCPR, obj->syncpr);
            if (r != ERROR_OK)
                return r;
        }
        /*
         * ETMv4 spec mandates programming the registers below whenever they
         * are implemented. their reset values are UNKNOWN. All are set for
         * continuous mode for simplicity.
         */
        if (caps->numrspair) {
            r = etmv4_queue_write32(obj, ETMV4_TRCEVENTCTL0R, 0);
            if (r != ERROR_OK)
                return r;
        }
        r = etmv4_queue_write32(obj, ETMV4_TRCEVENTCTL1R, 0);
        if (r != ERROR_OK)
            return r;
        if (caps->stallctl) {
            r = etmv4_queue_write32(obj, ETMV4_TRCSTALLCTLR, 0);
            if (r != ERROR_OK)
                return r;
        }
        if (caps->numacpairs) {
            r = etmv4_queue_write32(obj, ETMV4_TRCVIIECTLR, 0);
            if (r != ERROR_OK)
                return r;
            r = etmv4_queue_write32(obj, ETMV4_TRCVISSCTLR, 0);
            if (r != ERROR_OK)
                return r;
        }
        if (caps->numpc) {
            r = etmv4_queue_write32(obj, ETMV4_TRCVIPCSSCTLR, 0);
            if (r != ERROR_OK)
                return r;
        }
    }

    return etmv4_dap_run(obj);
}

static int etmv4_target_callback_event_handler(struct target *target,
        enum target_event event, void *priv)
{
    int r;
    struct etmv4_object *obj = priv;

    switch (event) {
    case TARGET_EVENT_RESET_END:
        if (!obj->initialised)
            return ERROR_OK;
        obj->enabled = false;
        /* Reset re-locks the component; re-unlock before re-committing. */
        r = etmv4_unlock(obj);
        if (r != ERROR_OK) {
            LOG_ERROR("ETMv4 %s: failed to unlock component after reset", obj->name);
            return r;
        }
        return etmv4_commit_config(obj, true);
    case TARGET_EVENT_RESUME_START:
        if (!obj->trace_requested || obj->enabled)
            return ERROR_OK;
        r = etmv4_validate_config(obj);
        if (r != ERROR_OK)
            return r;
        r = etmv4_commit_config(obj, false);
        if (r != ERROR_OK)
            return r;
        r = etmv4_start_trace_unit(obj);
        if (r != ERROR_OK)
            return r;
        obj->enabled = true;
        return ERROR_OK;
    case TARGET_EVENT_HALTED:
        if (!obj->enabled)
            return ERROR_OK;
        r = etmv4_stop_trace_unit(obj);
        if (r != ERROR_OK)
            return r;
        obj->enabled = false;
        return ERROR_OK;
    default:
        return ERROR_OK;
    }
}

static int etmv4_instance_init(struct etmv4_object *obj)
{
    int retval = etmv4_unlock(obj);
    if (retval != ERROR_OK) {
        LOG_ERROR("ETMv4 %s: failed to unlock component", obj->name);
        return retval;
    }
    retval = etmv4_validate_identity(obj);
    if (retval != ERROR_OK)
        return retval;
    retval = etmv4_read_capabilities(obj);
    if (retval != ERROR_OK)
        return retval;
    etmv4_apply_caps_config(obj);
    retval = etmv4_validate_config(obj);
    if (retval != ERROR_OK)
        return retval;
    retval = etmv4_stop_trace_unit(obj);
    if (retval != ERROR_OK)
        return retval;
    retval = etmv4_commit_config(obj, true);
    if (retval != ERROR_OK) {
        LOG_ERROR("ETMv4 %s: unable to write configuration to device", obj->name);
        return retval;
    }
    LOG_INFO("ETMv4 %s: base=0x%08" PRIx32 " AP=%" PRIu64 " traceid=%" PRIu32,
            obj->name, obj->spot.base, obj->spot.ap_num, obj->traceid);
    obj->initialised = true;
    return ERROR_OK;
}

/****************************************
 * Instance cmd handlers
 ****************************************/

COMMAND_HANDLER(etmv4_instance_init_handler)
{
    struct etmv4_object *obj = CMD_DATA;
    if (obj->initialised)
        return ERROR_OK;
    return etmv4_instance_init(obj);
}

COMMAND_HANDLER(etmv4_enable_handler)
{
    struct etmv4_object *obj = CMD_DATA;
    if (!obj->initialised) {
        LOG_ERROR("ETMv4 %s: not initialised, run '%s init' first",
                obj->name, obj->name);
        return ERROR_FAIL;
    }
    obj->trace_requested = true;
    return ERROR_OK;
}

/*
 * No hardware access here: the trace unit cannot be assumed reachable while
 * the core runs. A still-running trace unit is stopped by the next HALTED
 * event and, with the request cleared, is not re-armed afterwards.
 */
COMMAND_HANDLER(etmv4_disable_handler)
{
    struct etmv4_object *obj = CMD_DATA;
    if (obj->enabled)
        LOG_DEBUG("ETMv4 %s: tracing stops at the next halt", obj->name);
    obj->trace_requested = false;
    return ERROR_OK;
}

enum etmv4_cfg_param {
    ETMV4_CFG_TRACEID,
    ETMV4_CFG_CYCLECOUNT,
    ETMV4_CFG_TIMESTAMP,
    ETMV4_CFG_RETSTACK,
    ETMV4_CFG_BRANCH_BROADCAST,
    ETMV4_CFG_DATA_ADDR,
    ETMV4_CFG_DATA_VALUE,
};

static const struct jim_nvp nvp_etmv4_cfg_opts[] = {
    {.name = "-traceid",          .value = ETMV4_CFG_TRACEID},
    {.name = "-cyclecount",       .value = ETMV4_CFG_CYCLECOUNT},
    {.name = "-timestamp",        .value = ETMV4_CFG_TIMESTAMP},
    {.name = "-retstack",         .value = ETMV4_CFG_RETSTACK},
    {.name = "-branch-broadcast", .value = ETMV4_CFG_BRANCH_BROADCAST},
    {.name = "-data-addr",        .value = ETMV4_CFG_DATA_ADDR},
    {.name = "-data-value",       .value = ETMV4_CFG_DATA_VALUE},
    {.name = "-dap",              .value = -1},
    {.name = "-ap-num",           .value = -1},
    {.name = "-baseaddr",         .value = -1},
    {.name = NULL,                .value = -1},
};

static const struct jim_nvp nvp_bool_opts [] = {
	{ .name = "on",             .value = 1 },
	{ .name = "yes",            .value = 1 },
	{ .name = "true",           .value = 1 },
	{ .name = "off",            .value = 0 },
	{ .name = "no",             .value = 0 },
	{ .name = "false",          .value = 0 },
	{ .name = NULL,             .value = -1 },
};

static int etmv4_stage_config(struct etmv4_object *obj, struct jim_getopt_info *goi)
{
    Jim_Interp *interp = goi->interp;
    bool config_spot = false;
    int e;

    while (goi->argc > 0) {
        Jim_SetEmptyResult(interp);

        int old_argc = goi->argc;
        e = adiv5_jim_mem_ap_spot_configure(&obj->spot, goi);
        if (old_argc > goi->argc)
            config_spot = true;
        if (e == JIM_OK)
            continue;
        if (e == JIM_ERR)
            return e;

        struct jim_nvp *n;
        e = jim_getopt_nvp(goi, nvp_etmv4_cfg_opts, &n);
        if (e != JIM_OK) {
            jim_getopt_nvp_unknown(goi, nvp_etmv4_cfg_opts, 1);
            return e;
        }

        switch (n->value) {
        case ETMV4_CFG_TRACEID: {
            jim_wide w;
            e = jim_getopt_wide(goi, &w);
            if (e != JIM_OK) {
                Jim_SetResultFormatted(interp,
                        "ETMv4 %s: bad value for trace ID", obj->name);
                return JIM_ERR;
            }
            if (w < ETMV4_TRACEID_MIN || w > ETMV4_TRACEID_MAX) {
                Jim_SetResultFormatted(interp,
                        "ETMv4 %s: trace ID must be within 1..111 (0x70-0x7F are reserved)",
                        obj->name);
                return JIM_ERR;
            }
            obj->traceid = (uint32_t)w;
            obj->pending_config.traceid_set = true;
            break;
        }
        case ETMV4_CFG_CYCLECOUNT: {
            Jim_Obj *o;
            e = jim_getopt_obj(goi, &o);
            if (e != JIM_OK)
                return e;
            struct jim_nvp *p;
            e = jim_nvp_name2value_obj(interp, nvp_bool_opts, o, &p);
            if (e != JIM_OK) {
                jim_wide w;
                if (Jim_GetWide(interp, o, &w) != JIM_OK) {
                    Jim_SetResultFormatted(interp,
                            "ETMv4 %s: -cyclecount expects on, off or a threshold",
                            obj->name);
                    return JIM_ERR;
                }
                if (w < 1 || w > (jim_wide)ETMV4_TRCCCCTLR_THRESHOLD_MASK) {
                    Jim_SetResultFormatted(interp,
                            "ETMv4 %s: cycle count threshold must be within 1..4095",
                            obj->name);
                    return JIM_ERR;
                }
                obj->configr.cci = 1;
                obj->cc_threshold = (uint32_t)w;
            } else {
                obj->configr.cci = p->value;
                obj->cc_threshold = 0;
            }
            obj->pending_config.config_set = true;
            break;
        }
        case ETMV4_CFG_TIMESTAMP:
            if (goi->isconfigure) {
                struct jim_nvp* p;
                e = jim_getopt_nvp(goi, nvp_bool_opts, &p);
                if (e != JIM_OK) {
                    jim_getopt_nvp_unknown(goi, nvp_bool_opts, 0);
                    return e;
                }
                obj->configr.ts = p->value;
                obj->pending_config.config_set = true;
            } else {
                if (goi->argc) {
	                Jim_WrongNumArgs(goi->interp, goi->argc, goi->argv, "NO PARAMS");
	                return JIM_ERR;
                }
            }
            break;
        case ETMV4_CFG_RETSTACK:
            if(goi->isconfigure) {
                struct jim_nvp* p;
                e = jim_getopt_nvp(goi, nvp_bool_opts, &p);
                if (e != JIM_OK) {
                    jim_getopt_nvp_unknown(goi, nvp_bool_opts, 0);
                    return e;
                }
                obj->configr.rs = p->value;
                obj->pending_config.config_set = true;
            } else {
                if (goi->argc) {
	                Jim_WrongNumArgs(goi->interp, goi->argc, goi->argv, "NO PARAMS");
	                return JIM_ERR;
                }
            }
            break;
        case ETMV4_CFG_BRANCH_BROADCAST:
            if(goi->isconfigure) {
                struct jim_nvp* p;
                e = jim_getopt_nvp(goi, nvp_bool_opts, &p);
                if (e != JIM_OK) {
                    jim_getopt_nvp_unknown(goi, nvp_bool_opts, 0);
                    return e;
                }
                obj->configr.bb = p->value;
                obj->pending_config.config_set = true;
            } else {
                if (goi->argc) {
	                Jim_WrongNumArgs(goi->interp, goi->argc, goi->argv, "NO PARAMS");
	                return JIM_ERR;
                }
            }
            break;
        case ETMV4_CFG_DATA_ADDR:
            if(goi->isconfigure) {
                struct jim_nvp* p;
                e = jim_getopt_nvp(goi, nvp_bool_opts, &p);
                if (e != JIM_OK) {
                    jim_getopt_nvp_unknown(goi, nvp_bool_opts, 0);
                    return e;
                }
                obj->configr.da = p->value;
                obj->configr.instp0 = (obj->configr.da || obj->configr.dv) ?
                    ETMV4_TRCCONFIGR_INSTP0_LDST : 0;
                obj->pending_config.config_set = true;
            } else {
                if (goi->argc) {
	                Jim_WrongNumArgs(goi->interp, goi->argc, goi->argv, "NO PARAMS");
	                return JIM_ERR;
                }
            }
            break;
        case ETMV4_CFG_DATA_VALUE:
            if(goi->isconfigure) {
                struct jim_nvp* p;
                e = jim_getopt_nvp(goi, nvp_bool_opts, &p);
                if (e != JIM_OK) {
                    jim_getopt_nvp_unknown(goi, nvp_bool_opts, 0);
                    return e;
                }
                obj->configr.dv = p->value;
                obj->configr.instp0 = (obj->configr.da || obj->configr.dv) ?
                    ETMV4_TRCCONFIGR_INSTP0_LDST : 0;
                obj->pending_config.config_set = true;
            } else {
                if (goi->argc) {
	                Jim_WrongNumArgs(goi->interp, goi->argc, goi->argv, "NO PARAMS");
	                return JIM_ERR;
                }
            }
            break;
        }
    }
    if (config_spot)
        obj->ap = dap_get_ap(obj->spot.dap, obj->spot.ap_num);
    return JIM_OK;
}

static int jim_etmv4_configure(Jim_Interp *interp, int argc, Jim_Obj *const *argv)
{
    struct jim_getopt_info goi;
    jim_getopt_setup(&goi, interp, argc - 1, argv + 1);

    struct command *cmd = jim_to_command(interp);
    if (!cmd)
        return JIM_ERR;
    struct etmv4_object *obj = cmd->jim_handler_data;

    if (goi.argc < 1) {
        Jim_WrongNumArgs(interp, 1, argv, "..options...");
        return JIM_ERR;
    }
    goi.isconfigure = 1;
    if (etmv4_stage_config(obj, &goi) != JIM_OK)
        return JIM_ERR;
    /* The capabilities are unknown before init; validation happens at the
     * commit points instead. */
    if (obj->initialised) {
        if (etmv4_validate_config(obj) != ERROR_OK)
            return JIM_ERR;
        LOG_DEBUG("ETMv4 %s: configuration staged, applied at the next commit point",
                obj->name);
    }
    return JIM_OK;
}

static const struct command_registration etmv4_instance_command_handlers[] = {
    {
        .name = "init",
        .mode = COMMAND_EXEC,
        .help = "Initialise this ETMv4 trace unit",
        .usage = "",
        .handler = etmv4_instance_init_handler,
    },
    {
        .name = "enable",
        .mode = COMMAND_EXEC,
        .help = "Request tracing; the trace unit is programmed and started "
                "at the next target resume",
        .usage = "",
        .handler = etmv4_enable_handler,
    },
    {
        .name = "disable",
        .mode = COMMAND_EXEC,
        .help = "Cancel the trace request; the trace unit is stopped "
                "at the next target halt",
        .usage = "",
        .handler = etmv4_disable_handler,
    },
    {
        .name = "configure",
        .mode = COMMAND_ANY,
        .help = "Configure ETMv4 parameters",
        .usage = "[-traceid id] [-cyclecount on|off|threshold] [-timestamp on|off] "
                "[-retstack on|off] [-branch-broadcast on|off] [-data-addr on|off] "
                "[-data-value on|off] [-dap dap] [-ap-num num] [-baseaddr baseaddr]",
        .jim_handler = jim_etmv4_configure,
    },
    COMMAND_REGISTRATION_DONE,
};

/****************************************
 * Object creation and global commands
 ****************************************/

static int etmv4_create(struct jim_getopt_info *goi)
{
    Jim_Interp *interp = goi->interp;

    Jim_Obj *name_obj;
    if (jim_getopt_obj(goi, &name_obj) != JIM_OK)
        return JIM_ERR;
    const char *name = Jim_GetString(name_obj, NULL);

    struct etmv4_object *existing;
    list_for_each_entry(existing, &all_etmv4, lh) {
        if (!strcmp(name, existing->name)) {
            Jim_SetResultFormatted(interp, "ETMv4 '%s' already exists", name);
            return JIM_ERR;
        }
    }
    if (Jim_GetCommand(interp, name_obj, JIM_NONE)) {
        Jim_SetResultFormatted(interp, "command '%s' already exists", name);
        return JIM_ERR;
    }

    struct etmv4_object *obj = calloc(1, sizeof(*obj));
    if (!obj) {
        Jim_SetResultString(interp, "out of memory", -1);
        return JIM_ERR;
    }
    obj->name = strdup(name);
    if (!obj->name) {
        free(obj);
        Jim_SetResultString(interp, "out of memory", -1);
        return JIM_ERR;
    }

    adiv5_mem_ap_spot_init(&obj->spot);
    memcpy(obj->trcidr, etmv4_trcidr_defaults, sizeof(obj->trcidr));
    obj->traceid = 1;
    obj->victlr.word = 0x00000201U;
    obj->syncpr = ETMV4_TRCSYNCPR_DEFAULT;
    obj->configr.word = 0x00000001U;
    /* branch broadcast defaults on; degraded at init when not implemented */
    obj->configr.bb = 1;

    goi->isconfigure = 1;
    if (etmv4_stage_config(obj, goi) != JIM_OK) {
        free(obj->name);
        free(obj);
        return JIM_ERR;
    }

    if (!obj->ap) {
        Jim_SetResultString(interp,
                "-dap and -ap-num are mandatory when creating an ETMv4", -1);
        free(obj->name);
        free(obj);
        return JIM_ERR;
    }
    if (!obj->spot.base) {
        Jim_SetResultString(interp,
                "-baseaddr is mandatory when creating an ETMv4", -1);
        dap_put_ap(obj->ap);
        free(obj->name);
        free(obj);
        return JIM_ERR;
    }

    if (target_register_event_callback(etmv4_target_callback_event_handler,
            obj) != ERROR_OK) {
        Jim_SetResultString(interp, "cannot register target event callback", -1);
        dap_put_ap(obj->ap);
        free(obj->name);
        free(obj);
        return JIM_ERR;
    }

    const struct command_registration etmv4_instance_commands[] = {
        {
            .name = obj->name,
            .mode = COMMAND_ANY,
            .help = "ETMv4 instance command group",
            .usage = "",
            .chain = etmv4_instance_command_handlers,
        },
        COMMAND_REGISTRATION_DONE,
    };
    struct command_context *cmd_ctx = current_command_context(interp);
    assert(cmd_ctx);

    if (register_commands_with_data(cmd_ctx, NULL, etmv4_instance_commands,
            obj) != ERROR_OK) {
        target_unregister_event_callback(etmv4_target_callback_event_handler, obj);
        dap_put_ap(obj->ap);
        free(obj->name);
        free(obj);
        return JIM_ERR;
    }

    list_add_tail(&obj->lh, &all_etmv4);
    LOG_DEBUG("ETMv4 %s: object created", obj->name);
    return JIM_OK;
}

static int jim_etmv4_create(Jim_Interp *interp, int argc, Jim_Obj *const *argv)
{
    struct jim_getopt_info goi;
    jim_getopt_setup(&goi, interp, argc - 1, argv + 1);

    if (goi.argc < 1) {
        Jim_WrongNumArgs(interp, 1, argv, "?name? ..options...");
        return JIM_ERR;
    }
    return etmv4_create(&goi);
}

COMMAND_HANDLER(handle_etmv4_names)
{
    struct etmv4_object *obj;

    if (CMD_ARGC != 0)
        return ERROR_COMMAND_SYNTAX_ERROR;

    list_for_each_entry(obj, &all_etmv4, lh)
        command_print(CMD, "%s", obj->name);

    return ERROR_OK;
}

COMMAND_HANDLER(handle_etmv4_init)
{
    return etmv4_init_all();
}

int etmv4_init_all(void)
{
    struct etmv4_object *obj;
    int retval = ERROR_OK;

    list_for_each_entry(obj, &all_etmv4, lh) {
        if (obj->initialised)
            continue;
        int r = etmv4_instance_init(obj);
        if (r != ERROR_OK) {
            LOG_ERROR("ETMv4 %s: init failed", obj->name);
            if (retval == ERROR_OK)
                retval = r;
        }
    }
    return retval;
}

int etmv4_cleanup_all(void)
{
    struct etmv4_object *obj, *tmp;

    list_for_each_entry_safe(obj, tmp, &all_etmv4, lh) {
        if (target_unregister_event_callback(etmv4_target_callback_event_handler,
                obj) != ERROR_OK)
            LOG_WARNING("ETMv4 %s: failed to unregister target event callback",
                    obj->name);
        list_del(&obj->lh);
        if (obj->ap) {
            dap_put_ap(obj->ap);
            obj->ap = NULL;
        }
        free(obj->name);
        free(obj);
    }
    return ERROR_OK;
}

static const struct command_registration etmv4_subcommand_handlers[] = {
    {
        .name = "create",
        .mode = COMMAND_ANY,
        .help = "Create a new ETMv4 object",
        .usage = "name -dap dap -ap-num num -baseaddr baseaddr [configure-options]",
        .jim_handler = jim_etmv4_create,
    },
    {
        .name = "names",
        .mode = COMMAND_ANY,
        .help = "List all ETMv4 object names",
        .usage = "",
        .handler = handle_etmv4_names,
    },
    {
        .name = "init",
        .mode = COMMAND_EXEC,
        .help = "Initialise all registered ETMv4 objects",
        .usage = "",
        .handler = handle_etmv4_init,
    },
    COMMAND_REGISTRATION_DONE,
};

static const struct command_registration etmv4_command_handlers[] = {
    {
        .name = "etmv4",
        .mode = COMMAND_ANY,
        .help = "CoreSight Embedded Trace Macrocell v4 commands",
        .usage = "",
        .chain = etmv4_subcommand_handlers,
    },
    COMMAND_REGISTRATION_DONE,
};

int arm_etmv4_register_commands(struct command_context *cmd_ctx)
{
    return register_commands(cmd_ctx, NULL, etmv4_command_handlers);
}
