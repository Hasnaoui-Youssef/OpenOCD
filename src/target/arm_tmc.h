/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * arm_coresight_tmc.h
 * CoreSight Trace Memory Controller (TMC) module.
 */

#ifndef OPENOCD_TARGET_ARM_CORESIGHT_TMC_H
#define OPENOCD_TARGET_ARM_CORESIGHT_TMC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "helper/command.h"
#include "helper/list.h"

#include "arm_adi_v5.h"

/* =========================================================================
 * APB register offsets
 * ========================================================================= */
#define TMC_RSZ         0x004u  /* RAM Size                  [RO] words     */
#define TMC_STS         0x00Cu  /* Status                    [RO]            */
#define TMC_RRD         0x010u  /* RAM Read Data             [RO]            */
#define TMC_RRP         0x014u  /* RAM Read Pointer          [RW]            */
#define TMC_RWP         0x018u  /* RAM Write Pointer         [RW]            */
#define TMC_TRG         0x01Cu  /* Trigger Counter           [RW]            */
#define TMC_CTL         0x020u  /* Control                   [RW]            */
#define TMC_RWD         0x024u  /* RAM Write Data            [WO]            */
#define TMC_MODE        0x028u  /* Mode                      [RW]            */
#define TMC_LBUFLEVEL   0x02Cu  /* Latched Buffer Fill Level [RO] words      */
#define TMC_CBUFLEVEL   0x030u  /* Current Buffer Fill Level [RO] words      */
#define TMC_BUFWM       0x034u  /* Buffer Level Water Mark   [RW] words      */
#define TMC_RRPHI       0x038u  /* RAM Read Pointer High     [RW] ETR only   */
#define TMC_RWPHI       0x03Cu  /* RAM Write Pointer High    [RW] ETR only   */
#define TMC_AXICTL      0x110u  /* AXI Control               [RW] ETR only   */
#define TMC_DBALO       0x118u  /* Data Buffer Address Low   [RW] ETR only   */
#define TMC_DBAHI       0x11Cu  /* Data Buffer Address High  [RW] ETR only   */
#define TMC_FFSR        0x300u  /* Formatter & Flush Status  [RO]            */
#define TMC_FFCR        0x304u  /* Formatter & Flush Control [RW]            */
#define TMC_PSCR        0x308u  /* Periodic Sync Counter     [RW]            */

#define TMC_STS_FULL        BIT(0)  /* Buffer has wrapped at least once      */
#define TMC_STS_TRIGGERED   BIT(1)  /* Trigger observed in the trace stream  */
#define TMC_STS_TMCREADY    BIT(2)  /* TMC in Stopped state; RRD may be read */
#define TMC_STS_FTEMPTY     BIT(3)  /* Formatter pipeline is empty           */
#define TMC_STS_EMPTY       BIT(4)  /* RAM is empty (all data read)          */
#define TMC_STS_MEMERR      BIT(5)  /* Memory error (ETR only)               */

#define TMC_CTL_TRACECAPTEN BIT(0)

#define TMC_MODE_CIRCULAR   0x0u
#define TMC_MODE_SWFIFO     0x1u
#define TMC_MODE_HWFIFO     0x2u

#define TMC_FFSR_FLINPROG   BIT(0)  /* Flush in progress                     */
#define TMC_FFSR_FTSTOPPED  BIT(1)  /* Formatter stopped                     */

/* =========================================================================
 * TMC_FFCR bits
 * [0]  EnFt         Enable ATB formatter
 * [1]  EnTI         Enable trigger insertion
 * [2]  FOnFlIn      Flush on FLUSHIN
 * [3]  FOnTrigEvt   Flush on trigger event
 * [4]  FlushMan     Manual flush (self-clearing)
 * [5]  TrigOnFlIn   Trigger on FLUSHIN
 * [6]  TrigOnTrigIn Trigger on TRIGIN
 * [7]  StopOnFl     Stop TMC after flush completes
 * [8]  StopOnTrigIn Stop TMC on TRIGIN
 * [9]  DrainBuffer  Drain via ATB master
 * ========================================================================= */
#define TMC_FFCR_ENFT           BIT(0)
#define TMC_FFCR_ENTI           BIT(1)
#define TMC_FFCR_FONLYIN        BIT(4)
#define TMC_FFCR_FONTRIGEVT     BIT(5)
#define TMC_FFCR_FLUSHMAN       BIT(6)  /* Self-clearing */
#define TMC_FFCR_TRIGONTRIGIN   BIT(8)
#define TMC_FFCR_TRIGONTRIGEVT  BIT(9)
#define TMC_FFCR_TRIGONFLIN     BIT(10)
#define TMC_FFCR_STOPONFL       BIT(12)
#define TMC_FFCR_STOPOTRIGEVT   BIT(13)
#define TMC_FFCR_DRAINBUFFER    BIT(14)

#define TMC_FFCR_CIRC_CONFIG (TMC_FFCR_TRIGONTRIGIN | TMC_FFCR_FONTRIGEVT | TMC_FFCR_STOPONFL | TMC_FFCR_ENFT | TMC_FFCR_ENTI)
//This 2 are the same value but are set as different defines just in case it changes
#define TMC_FFCR_HW_FIFO_CONFIG (TMC_FFCR_STOPOTRIGEVT | TMC_FFCR_TRIGONTRIGIN | TMC_FFCR_ENFT | TMC_FFCR_ENTI)
#define TMC_FFCR_SW_FIFO_CONFIG (TMC_FFCR_STOPOTRIGEVT | TMC_FFCR_TRIGONTRIGIN | TMC_FFCR_ENFT | TMC_FFCR_ENTI)

/* =========================================================================
 * DEVTYPE[3:0] = 0x1: Trace Sink (ETB/ETR)
 * DEVTYPE[3:0] = 0x2: Trace Sink (ETF)
 * DEVTYPE[7:4] = 0x2: Buffer (ETB/ETR)
 * DEVTYPE[7:4] = 0x3: Router (ETF)
 *
 * DEVID[7:6] CONFIGTYPE:  0=ETB  1=ETR  2=ETF
 * ========================================================================= */
#define TMC_DEVTYPE_MAJOR_MASK      0x0Fu
#define TMC_DEVTYPE_SUB_MASK        0xF0u
#define TMC_DEVTYPE_MAJOR_SINK      0x01u
#define TMC_DEVTYPE_MAJOR_LINK      0x02u
#define TMC_DEVTYPE_SUB_BUFFER      0x20u
#define TMC_DEVTYPE_SUB_ROUTER      0x30u
#define TMC_DEVID_CFGTYPE_SHIFT     6u
#define TMC_DEVID_CFGTYPE_MASK      (0x3u << TMC_DEVID_CFGTYPE_SHIFT)
#define TMC_CFGTYPE_ETB             0x0u
#define TMC_CFGTYPE_ETR             0x1u
#define TMC_CFGTYPE_ETF             0x2u

#define TMC_RRD_SENTINEL            0xFFFFFFFFu
#define TMC_POLL_TIMEOUT_MS         2000u

enum tmc_mode {
	TMC_MODE_CIRC    = TMC_MODE_CIRCULAR,
	TMC_MODE_SW_FIFO = TMC_MODE_SWFIFO,
	TMC_MODE_HW_FIFO = TMC_MODE_HWFIFO,
};

enum tmc_config_type {
	TMC_CONFIG_ETB = TMC_CFGTYPE_ETB,
	TMC_CONFIG_ETR = TMC_CFGTYPE_ETR,
	TMC_CONFIG_ETF = TMC_CFGTYPE_ETF,
};

/*
 * This should represent the TMC's architectural state machine
 * Configuration should happen in the disabled state,
 * and data consumption should happen in the stopped state
 *
 * TMC_STOPPING and TMC_DISABLING are intentionally never assigned:
 * stops are always performed synchronously (poll to completion), so
 * Stopping collapses into Stopped from this driver's point of view,
 * and no emergency-abort path (clearing TraceCaptEn while Running) is
 * implemented, so Disabling never occurs.
 * */
enum tmc_state {
    TMC_DISABLED,
    TMC_STOPPED,
    TMC_DISABLING,
    TMC_STOPPING,
    TMC_RUNNING,
};

//For validation purposes, as well as caching changes
struct tmc_config_options {
    bool mode_set;
    bool bufwm_set;
    bool etr_size_set;
    bool etr_addr_set;
    bool axi_cache_set;
    bool axi_cache_alloc_set;
    bool axi_other_set;
};

//Assert this is 32bits in size so we can directly commit to memory
union tmc_axi_config {
  struct {
    uint32_t res_0: 20;
    uint32_t write_burstlen: 4;
    uint32_t scatter_mode: 1;
    uint32_t res_1: 1;
    uint32_t cache_alloc_w: 1;
    uint32_t cache_alloc_r: 1;
    uint32_t cache_en: 1;
    uint32_t bufferable: 1;
    uint32_t secure: 1;
    uint32_t privileged:1 ;
  };
  uint32_t word;
};

struct tmc_etr_config {
    uint64_t addr;
    uint32_t size;
    union tmc_axi_config axi_config;
};
static_assert(sizeof(struct tmc_etr_config) == 2 * sizeof(uint64_t), "ETR config should be 16 bytes");

struct tmc_object {
	struct list_head            lh;
	char                        *name;
	bool                        initialised;
	bool                        capture_requested;
    struct tmc_config_options   pending_config;

	struct adiv5_mem_ap_spot    spot;
	struct adiv5_ap             *ap;

	enum tmc_config_type        config_type;
	enum tmc_mode               mode;
    enum tmc_state              state;

	uint32_t                    bufwm;
	uint32_t                    ram_size_words;

    struct tmc_etr_config       etr_config;

    /*
     * Trace output sink. out_filename holds the raw -output string and
     * selects the sink: ":PORT" starts a TCP server, anything else non-empty
     * is a file. The two are mutually exclusive by construction. The port
     * substring handed to remove_service() points into out_filename, so that
     * buffer must outlive the service.
     */
    char                        *out_filename;
    FILE                        *file;
    struct list_head            connections;
    bool                        en_capture;
};


int arm_tmc_register_commands(struct command_context *cmd_ctx);

int tmc_init_all(void);

int tmc_cleanup_all(void);

struct tmc_object *tmc_find_by_name(const char *name);
void tmc_for_each(void (*fn)(struct tmc_object *obj, void *arg), void *arg);

int tmc_open_output(struct tmc_object *obj);
void tmc_close_output(struct tmc_object *obj);
int tmc_extract_data(struct tmc_object *obj);
int tmc_stage_config(struct tmc_object *obj, struct jim_getopt_info *goi);
int tmc_validate_config(struct tmc_object *obj);
int tmc_commit_config(struct tmc_object *obj, bool override);

extern const struct command_registration tmc_command_handlers[];

#endif /* OPENOCD_TARGET_ARM_CORESIGHT_TMC_H */
