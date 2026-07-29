/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * arm_etmv4.h
 * CoreSight Embedded Trace Macrocell v4 (ETMv4) module.
 * Register layout per ARM IHI0064 (ETMv4 Architecture Specification).
 */

#ifndef OPENOCD_TARGET_ARM_ETMV4_H
#define OPENOCD_TARGET_ARM_ETMV4_H

#include <stdint.h>
#include <stdbool.h>
#include "helper/command.h"
#include "helper/bits.h"

/* =========================================================================
 * Trace unit register offsets
 * ========================================================================= */
#define ETMV4_TRCPRGCTLR        0x004u  /* Programming Control          [RW] */
#define ETMV4_TRCSTATR          0x00Cu  /* Trace Status                 [RO] */
#define ETMV4_TRCCONFIGR        0x010u  /* Trace Configuration          [RW] */
#define ETMV4_TRCEVENTCTL0R     0x020u  /* Event Control 0              [RW] */
#define ETMV4_TRCEVENTCTL1R     0x024u  /* Event Control 1              [RW] */
#define ETMV4_TRCSTALLCTLR      0x02Cu  /* Stall Control                [RW] */
#define ETMV4_TRCTSCTLR         0x030u  /* Timestamp Control            [RW] */
#define ETMV4_TRCSYNCPR         0x034u  /* Synchronization Period       [RW*]*/
#define ETMV4_TRCCCCTLR         0x038u  /* Cycle Count Control          [RW] */
#define ETMV4_TRCBBCTLR         0x03Cu  /* Branch Broadcast Control     [RW] */
#define ETMV4_TRCTRACEIDR       0x040u  /* ATB Trace ID                 [RW] */
#define ETMV4_TRCVICTLR         0x080u  /* ViewInst Main Control        [RW] */
#define ETMV4_TRCVIIECTLR       0x084u  /* ViewInst Include/Exclude     [RW] */
#define ETMV4_TRCVISSCTLR       0x088u  /* ViewInst Start/Stop          [RW] */
#define ETMV4_TRCVIPCSSCTLR     0x08Cu  /* ViewInst Start/Stop PE Comp  [RW] */
#define ETMV4_TRCVDCTLR         0x0A0u  /* ViewData Main Control        [RW] */
#define ETMV4_TRCVDSACCTLR      0x0A4u  /* ViewData Include/Exclude SAC [RW] */
#define ETMV4_TRCVDARCCTLR      0x0A8u  /* ViewData Include/Exclude ARC [RW] */
#define ETMV4_TRCIDR8           0x180u  /* ID Register 8                [RO] */
#define ETMV4_TRCIDR9           0x184u  /* ID Register 9                [RO] */
#define ETMV4_TRCIDR10          0x188u  /* ID Register 10               [RO] */
#define ETMV4_TRCIDR11          0x18Cu  /* ID Register 11               [RO] */
#define ETMV4_TRCIDR12          0x190u  /* ID Register 12               [RO] */
#define ETMV4_TRCIDR13          0x194u  /* ID Register 13               [RO] */
#define ETMV4_TRCIDR0           0x1E0u  /* ID Register 0                [RO] */
#define ETMV4_TRCIDR1           0x1E4u  /* ID Register 1                [RO] */
#define ETMV4_TRCIDR2           0x1E8u  /* ID Register 2                [RO] */
#define ETMV4_TRCIDR3           0x1ECu  /* ID Register 3                [RO] */
#define ETMV4_TRCIDR4           0x1F0u  /* ID Register 4                [RO] */
#define ETMV4_TRCIDR5           0x1F4u  /* ID Register 5                [RO] */
#define ETMV4_TRCIDR6           0x1F8u  /* ID Register 6                [RO] */
#define ETMV4_TRCIDR7           0x1FCu  /* ID Register 7                [RO] */
#define ETMV4_TRCAUTHSTATUS     0xFB8   /* Authentication Status        [RO] */

#define ETMV4_NUM_TRCIDR        14

#define ETMV4_TRCPRGCTLR_EN         BIT(0)  /* Trace unit enable */

#define ETMV4_TRCSTATR_IDLE         BIT(0)  /* Trace unit is idle */
#define ETMV4_TRCSTATR_PMSTABLE     BIT(1)  /* Programmers' model is stable */

/* Bit-accurate representation of TRCCONFIGR */
union etmv4_trcconfigr {
    struct {
        uint32_t res1 : 1;      /* [0] */
        uint32_t instp0 : 2;    /* [2:1]   load/store as P0 instructions */
        uint32_t bb : 1;        /* [3]     branch broadcast mode */
        uint32_t cci : 1;       /* [4]     cycle counting */
        uint32_t res0_0 : 1;    /* [5] */
        uint32_t cid : 1;       /* [6]     context ID tracing */
        uint32_t vmid : 1;      /* [7]     virtual context ID tracing */
        uint32_t cond : 3;      /* [10:8]  conditional instruction tracing */
        uint32_t ts : 1;        /* [11]    global timestamp tracing */
        uint32_t rs : 1;        /* [12]    return stack */
        uint32_t qe : 2;        /* [14:13] Q element enable */
        uint32_t vmidopt : 1;   /* [15]    virtual context ID selection */
        uint32_t da : 1;        /* [16]    data address tracing */
        uint32_t dv : 1;        /* [17]    data value tracing */
        uint32_t res0_1 : 14;   /* [31:18] */
    };
    uint32_t word;
};

#define ETMV4_TRCCONFIGR_INSTP0_LDST    0x3u    /* loads and stores as P0 */
#define ETMV4_TRCCONFIGR_COND_ALL       0x7u    /* trace all conditional instructions */

/* Bit-accurate representation of TRCVICTLR */
union etmv4_trcvictlr {
    struct {
        uint32_t event : 8;      /* [7:0]   ViewInst event selector */
        uint32_t res0_0 : 1;     /* [8] */
        uint32_t ssstatus : 1;   /* [9]     start/stop logic state; 1 = started */
        uint32_t trcreset : 1;   /* [10]    always trace reset exceptions */
        uint32_t trcerr : 1;     /* [11]    always trace system error exceptions */
        uint32_t res0_1 : 4;     /* [15:12] */
        uint32_t exlevel_s : 4;  /* [19:16] Secure EL trace disable */
        uint32_t exlevel_ns : 4; /* [23:20] Non-secure EL trace disable */
        uint32_t res0_2 : 8;     /* [31:24] */
    };
    uint32_t word;
};

struct etmv4_decode_regs {
    uint32_t trcidr0;
    uint32_t trcidr1;
    uint32_t trcidr2;
    uint32_t trcidr3;
    uint32_t trcidr4;
    uint32_t trcidr5;
    uint32_t trcidr6;
    uint32_t trcidr7;
    uint32_t trcidr8;
    uint32_t trcidr9;
    uint32_t trcidr10;
    uint32_t trcidr11;
    uint32_t trcidr12;
    uint32_t trcidr13;
    uint32_t trcconfigr;
    uint32_t trctraceidr;
    uint32_t trcauthstatus;
};


/* Event selector value: resource selector 1, hardwired to always TRUE */
#define ETMV4_EVENT_TRUE            0x01u

#define ETMV4_TRCCCCTLR_THRESHOLD_MASK  0x00000FFFu

/* TRCIDR0: tracing capabilities */
#define ETMV4_TRCIDR0_COMMOPT           BIT(29)     /* Commit mode 1 */
#define ETMV4_TRCIDR0_TSSIZE_MASK       0x1F000000u /* Global timestamp size, bytes */
#define ETMV4_TRCIDR0_TSSIZE_SHIFT      24u
#define ETMV4_TRCIDR0_TSMARK            BIT(23)     /* Timestamp Marker elements */
#define ETMV4_TRCIDR0_BF_MASK           0x000C0000u /* Branch future support */
#define ETMV4_TRCIDR0_BF_SHIFT          18u
#define ETMV4_TRCIDR0_TRCEXDATA         BIT(17)     /* Exception data transfer tracing */
#define ETMV4_TRCIDR0_QSUPP_MASK        0x00018000u /* Q element support */
#define ETMV4_TRCIDR0_QSUPP_SHIFT       15u
#define ETMV4_TRCIDR0_QFILT             BIT(14)     /* Q element filtering */
#define ETMV4_TRCIDR0_CONDTYPE_MASK     0x00003000u /* Conditional trace form */
#define ETMV4_TRCIDR0_CONDTYPE_SHIFT    12u
#define ETMV4_TRCIDR0_NUMEVENT_MASK     0x00000C00u /* Events supported minus one */
#define ETMV4_TRCIDR0_NUMEVENT_SHIFT    10u
#define ETMV4_TRCIDR0_RETSTACK          BIT(9)      /* Return stack */
#define ETMV4_TRCIDR0_TRCCCI            BIT(7)      /* Cycle counting */
#define ETMV4_TRCIDR0_TRCCOND           BIT(6)      /* Conditional instruction tracing */
#define ETMV4_TRCIDR0_TRCBB             BIT(5)      /* Branch broadcast tracing */
#define ETMV4_TRCIDR0_TRCDATA_MASK      0x00000018u /* Data tracing */
#define ETMV4_TRCIDR0_TRCDATA_SHIFT     3u
#define ETMV4_TRCIDR0_INSTP0_MASK       0x00000006u /* Load/store as P0 elements */
#define ETMV4_TRCIDR0_INSTP0_SHIFT      1u

/* TRCIDR1: trace unit architecture */
#define ETMV4_TRCIDR1_DESIGNER_MASK     0xFF000000u /* JEP106-style designer code */
#define ETMV4_TRCIDR1_DESIGNER_SHIFT    24u
#define ETMV4_TRCIDR1_ARCHMAJ_MASK      0x00000F00u
#define ETMV4_TRCIDR1_ARCHMAJ_SHIFT     8u
#define ETMV4_TRCIDR1_ARCHMIN_MASK      0x000000F0u
#define ETMV4_TRCIDR1_ARCHMIN_SHIFT     4u
#define ETMV4_TRCIDR1_REVISION_MASK     0x0000000Fu
#define ETMV4_TRCIDR1_REVISION_SHIFT    0u
#define ETMV4_ARCH_MAJOR                0x4u

/* TRCIDR2: comparator value and counter sizes; size fields are in bytes */
#define ETMV4_TRCIDR2_WFXMODE           BIT(31)     /* WFI/WFE traced as branches */
#define ETMV4_TRCIDR2_VMIDOPT_MASK      0x60000000u /* VMID selection options */
#define ETMV4_TRCIDR2_VMIDOPT_SHIFT     29u
#define ETMV4_TRCIDR2_CCSIZE_MASK       0x1E000000u /* Cycle counter bits minus 12 */
#define ETMV4_TRCIDR2_CCSIZE_SHIFT      25u
#define ETMV4_TRCIDR2_DVSIZE_MASK       0x01F00000u /* Data value size */
#define ETMV4_TRCIDR2_DVSIZE_SHIFT      20u
#define ETMV4_TRCIDR2_DASIZE_MASK       0x000F8000u /* Data address size */
#define ETMV4_TRCIDR2_DASIZE_SHIFT      15u
#define ETMV4_TRCIDR2_VMIDSIZE_MASK     0x00007C00u /* VMID size */
#define ETMV4_TRCIDR2_VMIDSIZE_SHIFT    10u
#define ETMV4_TRCIDR2_CIDSIZE_MASK      0x000003E0u /* Context ID size */
#define ETMV4_TRCIDR2_CIDSIZE_SHIFT     5u
#define ETMV4_TRCIDR2_IASIZE_MASK       0x0000001Fu /* Instruction address size */
#define ETMV4_TRCIDR2_IASIZE_SHIFT      0u

/*
 * TRCIDR3: NUMPROC is a split 5-bit field; bits [13:12] (HI) are the top two
 * bits, bits [30:28] (LO) the bottom three.
 */
#define ETMV4_TRCIDR3_NOOVERFLOW        BIT(31)     /* TRCSTALLCTLR.NOOVERFLOW */
#define ETMV4_TRCIDR3_NUMPROC_LO_MASK   0x70000000u
#define ETMV4_TRCIDR3_NUMPROC_LO_SHIFT  28u
#define ETMV4_TRCIDR3_SYSSTALL          BIT(27)     /* System supports PE stalling */
#define ETMV4_TRCIDR3_STALLCTL          BIT(26)     /* TRCSTALLCTLR implemented */
#define ETMV4_TRCIDR3_SYNCPR            BIT(25)     /* 1: sync period fixed, TRCSYNCPR is RO */
#define ETMV4_TRCIDR3_TRCERR            BIT(24)     /* TRCVICTLR.TRCERR supported */
#define ETMV4_TRCIDR3_EXLEVEL_NS_MASK   0x00F00000u /* Non-secure EL trace support */
#define ETMV4_TRCIDR3_EXLEVEL_NS_SHIFT  20u
#define ETMV4_TRCIDR3_EXLEVEL_S_MASK    0x000F0000u /* Secure EL trace support */
#define ETMV4_TRCIDR3_EXLEVEL_S_SHIFT   16u
#define ETMV4_TRCIDR3_NUMPROC_HI_MASK   0x00003000u
#define ETMV4_TRCIDR3_NUMPROC_HI_SHIFT  12u
#define ETMV4_TRCIDR3_CCITMIN_MASK      0x00000FFFu /* Min TRCCCCTLR.THRESHOLD */
#define ETMV4_TRCIDR3_CCITMIN_SHIFT     0u

/* TRCIDR4: resource counts */
#define ETMV4_TRCIDR4_NUMVMIDC_MASK     0xF0000000u /* VMID comparators */
#define ETMV4_TRCIDR4_NUMVMIDC_SHIFT    28u
#define ETMV4_TRCIDR4_NUMCIDC_MASK      0x0F000000u /* Context ID comparators */
#define ETMV4_TRCIDR4_NUMCIDC_SHIFT     24u
#define ETMV4_TRCIDR4_NUMSSCC_MASK      0x00F00000u /* Single-shot comparator controls */
#define ETMV4_TRCIDR4_NUMSSCC_SHIFT     20u
#define ETMV4_TRCIDR4_NUMRSPAIR_MASK    0x000F0000u /* Resource selection pairs minus one */
#define ETMV4_TRCIDR4_NUMRSPAIR_SHIFT   16u
#define ETMV4_TRCIDR4_NUMPC_MASK        0x0000F000u /* PE comparator inputs */
#define ETMV4_TRCIDR4_NUMPC_SHIFT       12u
#define ETMV4_TRCIDR4_SUPPDAC           BIT(8)      /* Data address comparisons */
#define ETMV4_TRCIDR4_NUMDVC_MASK       0x000000F0u /* Data value comparators */
#define ETMV4_TRCIDR4_NUMDVC_SHIFT      4u
#define ETMV4_TRCIDR4_NUMACPAIRS_MASK   0x0000000Fu /* Address comparator pairs */
#define ETMV4_TRCIDR4_NUMACPAIRS_SHIFT  0u

/* TRCIDR5: resource counts */
#define ETMV4_TRCIDR5_REDFUNCNTR        BIT(31)     /* Counter 0 is reduced-function */
#define ETMV4_TRCIDR5_NUMCNTR_MASK      0x70000000u /* Counters */
#define ETMV4_TRCIDR5_NUMCNTR_SHIFT     28u
#define ETMV4_TRCIDR5_NUMSEQSTATE_MASK  0x0E000000u /* Sequencer states */
#define ETMV4_TRCIDR5_NUMSEQSTATE_SHIFT 25u
#define ETMV4_TRCIDR5_LPOVERRIDE        BIT(23)     /* Low-power state override */
#define ETMV4_TRCIDR5_ATBTRIG           BIT(22)     /* ATB trigger support */
#define ETMV4_TRCIDR5_TRACEIDSIZE_MASK  0x003F0000u /* Trace ID width, 7 on ATB */
#define ETMV4_TRCIDR5_TRACEIDSIZE_SHIFT 16u
#define ETMV4_TRCIDR5_NUMEXTINSEL_MASK  0x00000E00u /* External input selectors */
#define ETMV4_TRCIDR5_NUMEXTINSEL_SHIFT 9u
#define ETMV4_TRCIDR5_NUMEXTIN_MASK     0x000001FFu /* External inputs */
#define ETMV4_TRCIDR5_NUMEXTIN_SHIFT    0u

/* DEVARCH.ARCHID value identifying an ETMv4 trace unit */
#define ETMV4_DEVARCH_ARCHID        0x4A13u

/* ATB trace IDs 0x00 and 0x70-0x7F are reserved by the trace formatter */
#define ETMV4_TRACEID_MIN           0x01u
#define ETMV4_TRACEID_MAX           0x6Fu

#define ETMV4_TRCSYNCPR_DEFAULT     0x0Cu   /* sync every 2^12 = 4096 bytes */

#define ETMV4_POLL_TIMEOUT_MS       2000u

int arm_etmv4_register_commands(struct command_context *cmd_ctx);

int etmv4_init_all(void);

int etmv4_cleanup_all(void);

/* struct etmv4_object stays private to arm_etmv4.c; callers outside this
 * file only ever hold the pointer, resolved by name below. */
struct etmv4_object;

struct etmv4_object *etmv4_find_by_name(const char *name);
void etmv4_for_each(void (*fn)(struct etmv4_object *obj, void *arg), void *arg);

int etmv4_instance_init(struct etmv4_object *obj);
int etmv4_enable(struct etmv4_object *obj);
int etmv4_disable(struct etmv4_object *obj);

const char *etmv4_object_name(const struct etmv4_object *obj);
bool etmv4_object_initialised(const struct etmv4_object *obj);
bool etmv4_object_enabled(const struct etmv4_object *obj);
bool etmv4_object_trace_requested(const struct etmv4_object *obj);
uint64_t etmv4_object_ap_num(const struct etmv4_object *obj);
uint32_t etmv4_object_base(const struct etmv4_object *obj);
uint32_t etmv4_object_traceid(const struct etmv4_object *obj);
uint32_t etmv4_object_trcidr(const struct etmv4_object *obj, uint32_t idx);
uint32_t etmv4_object_authstatus(const struct etmv4_object *obj);
struct etmv4_decode_regs etmv4_object_decode_regs(const struct etmv4_object *obj);

#endif /* OPENOCD_TARGET_ARM_ETMV4_H */
