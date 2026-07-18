// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "helper/time_support.h"
#include "helper/fileio.h"
#include "helper/list.h"
#include "helper/log.h"

#include "arm_adi_v5.h"
#include "arm_coresight.h"
#include "target.h"

#include "arm_tmc.h"

static LIST_HEAD(all_tmc);

static int jim_get_goi_obj(Jim_Interp *interp,
                           struct tmc_object **obj,
                           struct jim_getopt_info *goi) {
  jim_getopt_setup(goi, interp, interp->argc - 1, interp->argv + 1);
  struct command *cmd = jim_to_command(interp);
  if (!cmd) {
    return ERROR_FAIL;
  }
  *obj = cmd->jim_handler_data;
  return ERROR_OK;
}

/********************************************
 * Helpers for device access
 * *****************************************/

static int tmc_read32(struct tmc_object *obj, uint32_t offset, uint32_t *val) {
  return mem_ap_read_atomic_u32(obj->ap, obj->spot.base + offset, val);
}

static int tmc_write32(struct tmc_object *obj, uint32_t offset, uint32_t val) {
  return mem_ap_write_atomic_u32(obj->ap, obj->spot.base + offset, val);
}

static int tmc_queue_write32(struct tmc_object *obj, uint32_t offset,
                             uint32_t val) {
  return mem_ap_write_u32(obj->ap, obj->spot.base + offset, val);
}

static int tmc_queue_read32(struct tmc_object *obj, uint32_t offset,
                             uint32_t *val) {
  return mem_ap_read_u32(obj->ap, obj->spot.base + offset, val);
}

static int tmc_read_trace_buff(struct tmc_object *obj, uint8_t* buff, uint32_t count)
{
  return mem_ap_read_buf_noincr(obj->ap, buff, 4, count, TMC_RRD);
}

static int tmc_dap_run(struct tmc_object *obj) {
  return dap_run(obj->spot.dap);
}

static int tmc_commit_config(struct tmc_object *obj, bool override) {
    int r;
    if (override || obj->pending_config.mode_set) {
        r = tmc_queue_write32(obj, TMC_MODE, obj->mode);
        if (r != ERROR_OK) return r;
        obj->pending_config.mode_set = false;
    }
    if (override || obj->pending_config.bufwm_set) {
        r = tmc_queue_write32(obj, TMC_BUFWM, obj->bufwm);
        if (r != ERROR_OK) return r;
        obj->pending_config.bufwm_set = false;
    }
    if (override || obj->pending_config.etr_addr_set) {
        r = tmc_queue_write32(obj, TMC_DBALO, (obj->etr_config.addr & ~((uint32_t) 0U)));
        if (r != ERROR_OK) return r;
        if((obj->etr_config.addr >> 32) > 0) {
            r = tmc_queue_write32(obj, TMC_DBAHI, (obj->etr_config.addr >> 32) & 0xF);
        }
        obj->pending_config.etr_addr_set = false;
    }
    if (override || obj->pending_config.etr_size_set) {
        r = tmc_queue_write32(obj, TMC_RSZ, obj->etr_config.size);
        if (r != ERROR_OK) return r;
        obj->pending_config.etr_size_set = false;
    }
    if(override || (
            obj->pending_config.axi_other_set ||
            obj->pending_config.axi_cache_set ||
            obj->pending_config.axi_cache_alloc_set
            )) {
        r = tmc_queue_write32(obj, TMC_AXICTL, *(uint32_t*)&obj->etr_config.axi_config);
        if (r != ERROR_OK) return r;
        obj->pending_config.axi_other_set = false;
        obj->pending_config.axi_cache_set = false;
        obj->pending_config.axi_cache_alloc_set = false;
    }
    return tmc_dap_run(obj);
}
static int tmc_validate_config(struct tmc_object* obj) {
    if(obj->config_type != TMC_CONFIG_ETR &&
            ( obj->pending_config.axi_other_set ||
              obj->pending_config.etr_addr_set ||
              obj->pending_config.etr_size_set)) {
        return ERROR_TARGET_INIT_FAILED;
    }
    if(obj->mode == TMC_MODE_CIRC  && obj->pending_config.bufwm_set) {
        return ERROR_TARGET_INIT_FAILED;
    }

    if(obj->etr_config.axi_config.cache_en == 0 && obj->pending_config.axi_cache_alloc_set){
        return ERROR_TARGET_INIT_FAILED;
    }

    return ERROR_OK;
}

static int tmc_poll_bit(struct tmc_object *obj, uint32_t offset, uint32_t mask,
                        uint32_t expected, unsigned int timeout_ms) {
  int64_t deadline = timeval_ms() + (int64_t)timeout_ms;
  uint32_t val;

  do {
    int retval = tmc_read32(obj, offset, &val);
    if (retval != ERROR_OK)
      return retval;
    if ((val & mask) == expected)
      return ERROR_OK;
    alive_sleep(1);
  } while (timeval_ms() < deadline);

  return ERROR_TIMEOUT_REACHED;
}

/***************************************
 * Misc functions
 **************************************/
static struct tmc_trace_data_chunk* tmc_create_chunk(struct tmc_object *obj, uint32_t count)
{
  struct tmc_trace_data_chunk *chunk = calloc(1, sizeof(struct tmc_trace_data_chunk));
  if(!chunk)
      return NULL;
  INIT_LIST_HEAD(&chunk->lh);
  chunk->byte_count = count * sizeof(uint32_t);
  chunk->buff = malloc(chunk->byte_count);
  if(!chunk->buff){
      free(chunk);
      return NULL;
  }
  return chunk;
}

/**
 * This method assumes that the TMC is already stopped at this point.
 * This doesn't matter much for circular buffer mode since
 * we'll be polling the TMCREADY bit, however this can really degrade
 * performance and limit communication with the device if the TMC is still running
 * as we are using an atomic operation over and over again to check for the status
*/
static int tmc_extract_data(struct tmc_object* obj)
{
    int r;
    switch(obj->mode) {
    case TMC_MODE_CIRC: {
        r = tmc_poll_bit(obj, TMC_STS, TMC_STS_TMCREADY, TMC_STS_TMCREADY, TMC_POLL_TIMEOUT_MS);
        if (r != ERROR_OK)
            return r;

        if (obj->config_type == TMC_CONFIG_ETR && obj->etr_config.axi_config.scatter_mode) {
            LOG_ERROR("TMC %s: trace extraction is not supported in scatter-gather mode",
                    obj->name);
            return ERROR_NOT_IMPLEMENTED;
        }

        /*
         * CBUFLEVEL is only valid while TraceCaptEn is set, which still holds
         * here: CTL is not cleared until the buffer has been drained below.
         * By this point the stop sequence has padded the trace out to a whole
         * number of formatter frames, so the fill level covers every byte that
         * needs reading. LBUFLEVEL is unsuitable, it latches a maximum since
         * its own last read rather than the amount of readable data.
         */
        uint32_t sts_val, buff_level_words;
        r = tmc_queue_read32(obj, TMC_STS, &sts_val);
        if (r != ERROR_OK)
            return r;
        r = tmc_queue_read32(obj, TMC_CBUFLEVEL, &buff_level_words);
        if (r != ERROR_OK)
            return r;
        r = tmc_dap_run(obj);
        if (r != ERROR_OK)
            return r;

        /* Once the buffer has wrapped it stays full, so the whole RAM is live. */
        if (sts_val & TMC_STS_FULL)
            buff_level_words = (obj->config_type == TMC_CONFIG_ETR) ? obj->etr_config.size
                                                                    : obj->ram_size_words;

        if (buff_level_words == 0) {
            LOG_DEBUG("TMC %s: trace buffer empty, nothing to capture", obj->name);
            obj->state = TMC_DISABLED;
            return tmc_write32(obj, TMC_CTL, 0);
        }

        struct tmc_trace_data_chunk *chunk = tmc_create_chunk(obj, buff_level_words);
        if(!chunk) {
            return ERROR_FAIL;
        }
        r = tmc_read_trace_buff(obj, chunk->buff, buff_level_words);
        if (r != ERROR_OK) {
            LOG_ERROR("TMC %s: failed to read %" PRIu32 " words of trace data",
                    obj->name, buff_level_words);
            free(chunk->buff);
            free(chunk);
            return r;
        }
        list_add_tail(&chunk->lh, &obj->history.chunks);
        LOG_DEBUG("TMC %s: captured %" PRIu32 " bytes of trace data%s", obj->name,
                chunk->byte_count, (sts_val & TMC_STS_FULL) ? " (buffer wrapped)" : "");

        obj->state = TMC_DISABLED;
        return tmc_write32(obj, TMC_CTL, 0);
    }
    case TMC_MODE_SW_FIFO: {
        /*
         * Currently we cannot support this without a multi-threaded modal
         * This assumes that we are continuously reading data from the TMC
         * when the core is running and this just disables the TMC by
         * triggering a manual flush
        */
        uint32_t ffcr_val;
        r = tmc_read32(obj, TMC_FFCR, &ffcr_val);
        if (r != ERROR_OK)
            return r;
        r = tmc_queue_write32(obj, TMC_FFCR, ffcr_val | TMC_FFCR_STOPONFL);
        if (r != ERROR_OK)
            return r;
        r = tmc_queue_write32(obj, TMC_FFCR, ffcr_val | TMC_FFCR_STOPONFL | TMC_FFCR_FLUSHMAN);
        if (r != ERROR_OK)
            return r;
        r = tmc_dap_run(obj);
        if (r != ERROR_OK)
            return r;
        return ERROR_FAIL;
    }
    case TMC_MODE_HW_FIFO:
        //Should we even support this given the state of the probes?
        return ERROR_OK;
    default:
        return ERROR_FAIL;
    }
}

static int tmc_start_capture(struct tmc_object *obj) {
  int r;
  uint32_t ffcr_config_val;
  switch(obj->mode) {
  case TMC_MODE_CIRC:
    ffcr_config_val = TMC_FFCR_CIRC_CONFIG;
    break;
  case TMC_MODE_SW_FIFO:
    ffcr_config_val = TMC_FFCR_SW_FIFO_CONFIG;
    break;
  case TMC_MODE_HW_FIFO:
    ffcr_config_val = TMC_FFCR_HW_FIFO_CONFIG;
    break;
  default:
    return ERROR_FAIL;
  }
  r = tmc_queue_write32(obj, TMC_FFCR, ffcr_config_val);
  if (r != ERROR_OK)
    return r;
  r = tmc_queue_write32(obj, TMC_CTL, TMC_CTL_TRACECAPTEN);
  if (r != ERROR_OK)
    return r;
  r = tmc_dap_run(obj);
  if (r != ERROR_OK)
    return r;
  obj->state = TMC_RUNNING;
  return ERROR_OK;
}

/*
 * Called from TARGET_EVENT_HALTED. The TMC may already have reached
 * Stopped on its own (e.g. a HW trigger-driven stop fired before the
 * core halted), so poll TMCReady first. Only if that times out do we
 * force a stop via manual flush, then poll again before extracting.
 */
static int tmc_stop_and_extract(struct tmc_object *obj)
{
    int r;
    uint32_t ffcr_val, sts_val;

    r = tmc_poll_bit(obj, TMC_STS, TMC_STS_TMCREADY, TMC_STS_TMCREADY, TMC_POLL_TIMEOUT_MS);
    if (r != ERROR_OK) {
        r = tmc_read32(obj, TMC_FFCR, &ffcr_val);
        if (r != ERROR_OK)
            return r;
        r = tmc_write32(obj, TMC_FFCR, ffcr_val | TMC_FFCR_STOPONFL);
        r |= tmc_write32(obj, TMC_FFCR, ffcr_val | TMC_FFCR_STOPONFL | TMC_FFCR_FLUSHMAN);
        if (r != ERROR_OK)
            return r;
        r = tmc_poll_bit(obj, TMC_STS, TMC_STS_TMCREADY, TMC_STS_TMCREADY, TMC_POLL_TIMEOUT_MS);
        if (r != ERROR_OK)
            return r;
    }

    r = tmc_read32(obj, TMC_STS, &sts_val);
    if (r != ERROR_OK)
        return r;
    if (sts_val & TMC_STS_EMPTY) {
        obj->state = TMC_DISABLED;
        return tmc_write32(obj, TMC_CTL, 0);
    }
    obj->state = TMC_STOPPED;
    return tmc_extract_data(obj);
}

static int tmc_target_callback_event_handler(struct target *target,
        enum target_event event,
        void *priv)
{
    int r;
    struct tmc_object* obj = (struct tmc_object*) priv;
    switch(event) {
    case TARGET_EVENT_RESET_END:
        obj->state = TMC_DISABLED;
        return tmc_commit_config(obj, true);
    case TARGET_EVENT_RESUME_START:
        if (!obj->capture_requested || obj->state != TMC_DISABLED)
            return ERROR_OK;
        r = tmc_validate_config(obj);
        if(r != ERROR_OK)
            return r;
        r =  tmc_commit_config(obj, false);
        if(r != ERROR_OK)
            return r;
        return tmc_start_capture(obj);
    case TARGET_EVENT_HALTED:
        if (obj->state != TMC_RUNNING)
            return ERROR_OK;
        return tmc_stop_and_extract(obj);
    default:
        return ERROR_OK;
    }
}

static int tmc_unlock(struct tmc_object *obj) {
  uint32_t lsr;
  int retval = tmc_read32(obj, ARM_CS_LSR, &lsr);
  if (retval != ERROR_OK)
    return retval;

  if (!(lsr & ARM_CS_LSR_SLI)) {
    LOG_DEBUG("TMC %s: lock not enforced on this AP, skipping LAR", obj->name);
    return ERROR_OK;
  }
  return tmc_write32(obj, ARM_CS_LAR, ARM_CS_LAR_UNLOCK);
}

static int tmc_validate_identity(struct tmc_object *obj) {
  uint32_t devtype, devid;
  int retval;

  retval = tmc_queue_read32(obj, ARM_CS_C9_DEVID, &devid);
  if (retval != ERROR_OK) {
    LOG_ERROR("TMC %s: failed to read DEVID", obj->name);
    return retval;
  }
  retval = tmc_queue_read32(obj, ARM_CS_C9_DEVTYPE, &devtype);
  if (retval != ERROR_OK) {
    LOG_ERROR("TMC %s: failed to read DEVTYPE", obj->name);
    return retval;
  }
  retval = tmc_dap_run(obj);
  if (retval != ERROR_OK) {
    LOG_ERROR("TMC %s: failed to read DEVTYPE and DEVID", obj->name);
    return retval;
  }

  uint8_t major = (uint8_t)(devtype & TMC_DEVTYPE_MAJOR_MASK);
  uint8_t sub = (uint8_t)(devtype & TMC_DEVTYPE_SUB_MASK);
  uint32_t ct = (devid & TMC_DEVID_CFGTYPE_MASK) >> TMC_DEVID_CFGTYPE_SHIFT;

  LOG_DEBUG("TMC %s: DEVID : 0x%08" PRIx32, obj->name, devid);

  switch (ct) {
  case TMC_CFGTYPE_ETB:
  case TMC_CFGTYPE_ETR:
    obj->config_type = ct;
    if(major != TMC_DEVTYPE_MAJOR_SINK || sub != TMC_DEVTYPE_SUB_BUFFER) {
      LOG_ERROR("TMC %s: DEVTYPE 0x%08" PRIx32
                " is not an ETR|ETB expected major=0x1, sub=0x2",
                obj->name, devtype);
      return ERROR_FAIL;
    }
    break;
  case TMC_CFGTYPE_ETF:
    obj->config_type = ct;
    if(major != TMC_DEVTYPE_MAJOR_LINK || sub != TMC_DEVTYPE_SUB_ROUTER) {
      LOG_ERROR("TMC %s: DEVTYPE 0x%08" PRIx32
                " is not an ETF expected major=0x2, sub=0x3",
                obj->name, devtype);
      return ERROR_FAIL;
    }
    break;
  default:
    LOG_WARNING("TMC %s: unknown CONFIGTYPE %u", obj->name, ct);
    return ERROR_FAIL;
  }

  return ERROR_OK;
}

static int tmc_buff_init(struct tmc_object* obj)
{
    INIT_LIST_HEAD(&obj->history.chunks);
    return ERROR_OK;
}
static int tmc_buff_free(struct tmc_object* obj)
{
    struct tmc_trace_data_chunk *chunk, *temp;
    list_for_each_entry_safe(chunk, temp, &obj->history.chunks, lh)
    {
        list_del(&chunk->lh);
        free(chunk->buff);
        free(chunk);
    }
    return ERROR_OK;
}


static int tmc_instance_init(struct tmc_object *obj) {
  int retval;
  retval = tmc_unlock(obj);
  if (retval != ERROR_OK) {
    LOG_ERROR("TMC %s: failed to unlock component", obj->name);
    dap_put_ap(obj->ap);
    obj->ap = NULL;
    return retval;
  }
  retval = tmc_validate_identity(obj);
  if(retval != ERROR_OK) {
      LOG_ERROR("TMC %s: failed to validate component is TMC", obj->name);
      return retval;
  }

  retval = tmc_validate_config(obj);
  if(retval != ERROR_OK){
    LOG_ERROR("TMC %s: Configuration is not valid, verify parameters,"
            "possibly passing ETR params for ETB/ETF, AXI cache alloc without cache enabled,"
            "or buffer watermak level with Circular Buffer mode.", obj->name);
    return retval;
  }
  if (obj->config_type != TMC_CONFIG_ETR) {
    retval = tmc_read32(obj, TMC_RSZ, &obj->ram_size_words);
    if (retval != ERROR_OK) {
      LOG_ERROR("TMC %s: failed to read RSZ", obj->name);
      return retval;
    }
  }
  LOG_INFO("TMC %s (%s) base=0x%08" PRIx64 " AP=%u", obj->name,
           obj->config_type == TMC_CONFIG_ETB   ? "ETB"
           : obj->config_type == TMC_CONFIG_ETR ? "ETR"
                                                : "ETF",
           (uint64_t)obj->spot.base, (unsigned int)obj->spot.ap_num);
  if (obj->state != TMC_DISABLED) {
    LOG_ERROR("TMC %s: cannot commit initial configuration, device not disabled", obj->name);
    return ERROR_FAIL;
  }
  retval = tmc_commit_config(obj, true);
  if(retval != ERROR_OK) {
      LOG_ERROR("TMC %s: Unable to write configuration to device", obj->name);
      return retval;
  }
  obj->initialised = true;
  return ERROR_OK;
}

/****************************************
 * Instance cmd handlers
 ****************************************/

COMMAND_HANDLER(tmc_instance_init_handler) {
  struct tmc_object *obj = CMD_DATA;
  if (obj->initialised)
    return ERROR_OK;
  return tmc_instance_init(obj);
}

enum tmc_cfg_param {
  CFG_MODE,
  CFG_ETR_BUF_ADDR,
  CFG_ETR_BUF_SIZE,
  CFG_BUFWM,
  CFG_WRBL,
  CFG_CACHE_EN,
  CFG_WRITE_ALLOC,
  CFG_READ_ALLOC,
  CFG_BUFFERABLE,
  CFG_SECURE,
  CFG_PRIVILEGED,
  CFG_ETR_SCATTER_GATHER,
  CFG_AXI_CTL,
};

static const struct jim_nvp nvp_tmc_cfg_opts[] = {
    {.name = "-mode",               .value = CFG_MODE               },
    {.name = "-bufwm",              .value = CFG_BUFWM              },
    {.name = "-etr-buf-addr",       .value = CFG_ETR_BUF_ADDR       },
    {.name = "-etr-buf-size",       .value = CFG_ETR_BUF_SIZE       },
    {.name = "-etr-scatter-mode",   .value = CFG_ETR_SCATTER_GATHER },
    {.name = "-axi-wrbl",           .value = CFG_WRBL               },
    {.name = "-axi-cache-en",       .value = CFG_CACHE_EN           },
    {.name = "-axi-write-alloc",    .value = CFG_WRITE_ALLOC        },
    {.name = "-axi-read-alloc",     .value = CFG_READ_ALLOC         },
    {.name = "-axi-bufferable",     .value = CFG_BUFFERABLE         },
    {.name = "-axi-secure",         .value = CFG_SECURE             },
    {.name = "-axi-privileged",     .value = CFG_PRIVILEGED         },
    {.name = "-axi-ctl",            .value = CFG_AXI_CTL            },
    {.name = "-dap",                .value = -1                     },
    {.name = "-ap-num",             .value = -1                     },
    {.name = "-baseaddr",           .value = -1                     },
    {.name = NULL,                  .value = -1                     },
};

static const struct jim_nvp nvp_tmc_mode[] = {
    {.name = "circular", .value = TMC_MODE_CIRC},
    {.name = "sw-fifo", .value = TMC_MODE_SW_FIFO},
    {.name = "hw-fifo", .value = TMC_MODE_HW_FIFO},
    {.name = NULL, .value = -1},
};

static int tmc_stage_config(struct tmc_object* obj, struct jim_getopt_info *goi) {
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
    e = jim_getopt_nvp(goi, nvp_tmc_cfg_opts, &n);
    if (e != JIM_OK) {
      jim_getopt_nvp_unknown(goi, nvp_tmc_cfg_opts, 1);
      return e;
    }

    switch (n->value) {
    case CFG_MODE: {
        struct jim_nvp* m;
        e = jim_getopt_nvp(goi, nvp_tmc_mode, &m);
        if (e != JIM_OK) {
          Jim_SetResultFormatted(interp,
                  "TMC %s: bad value for mode",
                  obj->name);
          return JIM_ERR;
        }
        obj->mode = m->value;
        obj->pending_config.mode_set = true;
        break;
    }
    case CFG_ETR_BUF_ADDR:{
        jim_wide w;
        e = jim_getopt_wide(goi, &w);
        if( e != JIM_OK ) {
          Jim_SetResultFormatted(interp,
                  "TMC %s: bad value for ETR start address",
                  obj->name);
          return JIM_ERR;
        }
        obj->pending_config.etr_addr_set = true;
        //Should assert that jim_wide is 64 bits
        //This won't work for 32-bit systems but I doubt that's of any issue rn
        obj->etr_config.addr = (uint64_t) w;
        break;
    }
    case CFG_ETR_BUF_SIZE: {
        jim_wide w;
        e = jim_getopt_wide(goi, &w);
        if( e != JIM_OK ) {
          Jim_SetResultFormatted(interp,
                  "TMC %s: bad value for ETR size",
                  obj->name);
          return JIM_ERR;
        }
        obj->etr_config.size = (uint32_t) w;
        obj->pending_config.etr_size_set = true;
        break;
    }
    case CFG_BUFWM: {
        jim_wide w;
        e = jim_getopt_wide(goi, &w);
        if( e != JIM_OK ) {
          Jim_SetResultFormatted(interp,
                  "TMC %s: bad value for Buffer watermark level",
                  obj->name);
          return JIM_ERR;
        }
        obj->bufwm = (uint32_t) w;
        obj->pending_config.bufwm_set = true;
        break;
    }
    case CFG_WRBL: {
        jim_wide w;
        jim_getopt_wide(goi, &w);
        if( e != JIM_OK ) {
          Jim_SetResultFormatted(interp,
                  "TMC %s: cannot set value for AXI Write Burst Length",
                  obj->name);
          return JIM_ERR;
        }
        if(w > 0xF || w < 0) {
          Jim_SetResultFormatted(interp,
                  "TMC %s: bad value for AXI Write Burst Length",
                  obj->name);
          return JIM_ERR;
        }
        obj->etr_config.axi_config.write_burstlen = (uint32_t) w;
        obj->pending_config.axi_other_set = true;
        break;
    }
    case CFG_CACHE_EN: {
        obj->etr_config.axi_config.cache_en = 1;
        obj->pending_config.axi_cache_set = true;
        obj->pending_config.axi_other_set = true;
        break;
    }
    case CFG_WRITE_ALLOC: {
        obj->etr_config.axi_config.cache_alloc_w = 1;
        obj->pending_config.axi_cache_alloc_set = true;
        obj->pending_config.axi_other_set = true;
        break;
    }
    case CFG_READ_ALLOC: {
        obj->etr_config.axi_config.cache_alloc_r = 1;
        obj->pending_config.axi_cache_alloc_set = true;
        obj->pending_config.axi_other_set = true;
        break;
    }
    case CFG_BUFFERABLE: {
        obj->etr_config.axi_config.bufferable = 1;
        obj->pending_config.axi_other_set = true;
        break;
    }
    case CFG_SECURE: {
        obj->etr_config.axi_config.secure = 1;
        obj->pending_config.axi_other_set = true;
        break;
    }
    case CFG_PRIVILEGED: {
        obj->etr_config.axi_config.privileged = 1;
        obj->pending_config.axi_other_set = true;
        break;
    }
    case CFG_AXI_CTL: {
        jim_wide w;
        e = jim_getopt_wide(goi, &w);
        if(e != JIM_OK) {
          Jim_SetResultFormatted(interp,
                  "TMC %s: bad value for Buffer watermark level",
                  obj->name);
          return JIM_ERR;
        }
        obj->etr_config.axi_config.word = (uint32_t) w;
        if(w != 0) {
            if(obj->pending_config.axi_other_set) {
                Jim_SetResultFormatted(interp,
                        "TMC %s: cannot use -axi-ctl flag with other -axi flags",
                        obj->name);
                return JIM_ERR;
            }
            obj->pending_config.axi_other_set = 1;
        }
        if(w & 0x8U) {
            obj->pending_config.axi_cache_set = 1;
        }
        if(w & 0x30U) {
            obj->pending_config.axi_cache_alloc_set = 1;
        }
    }
    };
  }
  if (config_spot) {
    obj->ap = dap_get_ap(obj->spot.dap, obj->spot.ap_num);
  }
  return JIM_OK;
}

COMMAND_HANDLER(tmc_enable_handler) {
  struct tmc_object *obj = CMD_DATA;
  if (!obj->initialised)
    return ERROR_FAIL;
  obj->capture_requested = true;
  return ERROR_OK;
}

/*
 * TARGET_EVENT_HALTED already drains every Running session down to
 * Disabled on its own, so state == TMC_STOPPED here is the recovery
 * path for when that automatic drain didn't finish (e.g. a poll in
 * tmc_stop_and_extract timed out). This never force-stops a currently
 * Running capture itself.
 */
COMMAND_HANDLER(tmc_disable_handler)
{
    struct tmc_object* obj = CMD_DATA;
    obj->capture_requested = false;
    if (obj->state == TMC_STOPPED)
        return tmc_extract_data(obj);
    return ERROR_OK;
}

/*
 * Build "base.NN.ext" from "base.ext", or "base.NN" when the name carries no
 * extension. Only a '.' in the final path component counts as an extension.
 * Caller owns the returned string.
 */
static char *tmc_chunk_filename(const char *base, unsigned int index)
{
    const char *dot = strrchr(base, '.');
    const char *slash = strrchr(base, '/');

    if (dot && (!slash || dot > slash))
        return alloc_printf("%.*s.%02u%s", (int)(dot - base), base, index, dot);

    return alloc_printf("%s.%02u", base, index);
}

/*
 * Each chunk is one capture session, and so is an independently decodable
 * trace stream: the TMC stop sequence pads it out to a whole number of
 * formatter frames, and the trace source re-synchronises at the start of the
 * next session. Concatenating chunks would still deformat correctly, but it
 * would hide the execution discontinuity between sessions from the decoder,
 * which would then reconstruct control flow across a gap that never executed.
 * Each chunk therefore gets its own file.
 *
 * Chunks are released as they are written, so the history does not grow
 * without bound over a long debug session.
 */
COMMAND_HANDLER(tmc_trace_dump_handler)
{
    struct tmc_object* obj = CMD_DATA;
    struct tmc_trace_data_chunk *chunk, *tmp;
    unsigned int index = 0;
    uint64_t total = 0;

    if (CMD_ARGC != 1)
        return ERROR_COMMAND_SYNTAX_ERROR;

    if (list_empty(&obj->history.chunks)){
        command_print(CMD, "TMC %s: no trace data captured", obj->name);
        return ERROR_OK;
    }

    command_print(CMD, "chunk  length file");

    list_for_each_entry_safe(chunk, tmp, &obj->history.chunks, lh){
        struct fileio *file;
        size_t written;
        int r;

        char *path = tmc_chunk_filename(CMD_ARGV[0], index);
        if (!path) {
            LOG_ERROR("TMC %s: out of memory building chunk filename", obj->name);
            return ERROR_FAIL;
        }

        r = fileio_open(&file, path, FILEIO_WRITE, FILEIO_BINARY);
        if (r != ERROR_OK) {
            LOG_ERROR("TMC %s: cannot open '%s' for writing", obj->name, path);
            free(path);
            return r;
        }

        r = fileio_write(file, chunk->byte_count, chunk->buff, &written);
        if (r == ERROR_OK && written != chunk->byte_count) {
            LOG_ERROR("TMC %s: short write to '%s': %zu of %" PRIu32 " bytes",
                    obj->name, path, written, chunk->byte_count);
            r = ERROR_FILEIO_OPERATION_FAILED;
        }
        fileio_close(file);
        if (r != ERROR_OK) {
            free(path);
            return r;
        }

        command_print(CMD, "%-6u %-6" PRIu32 " %s", index, chunk->byte_count, path);
        free(path);

        total += chunk->byte_count;
        index++;

        list_del(&chunk->lh);
        free(chunk->buff);
        free(chunk);
    }

    command_print(CMD, "%s: %u chunks, %" PRIu64 " bytes", obj->name, index, total);
    return ERROR_OK;
}
static int jim_tmc_configure(Jim_Interp *interp, int argc, Jim_Obj *const *argv) {
  struct jim_getopt_info goi;
  struct tmc_object* obj;
  jim_getopt_setup(&goi, interp, argc - 1, argv + 1);
  int r = jim_get_goi_obj(interp, &obj, &goi);
  if(r != ERROR_OK)
      return JIM_ERR;

  if (goi.argc < 1) {
    Jim_WrongNumArgs(interp, 1, argv, "?name? ..options...");
    return JIM_ERR;
  }
  goi.isconfigure = 1;
  r = tmc_stage_config(obj, &goi);
  if(r != JIM_OK)
      return JIM_ERR;
  r = tmc_validate_config(obj);
  if(r != ERROR_OK)
      return JIM_ERR;
  if (obj->state == TMC_DISABLED) {
      r = tmc_commit_config(obj, false);
      if(r != ERROR_OK)
          return JIM_ERR;
  } else {
      LOG_INFO("TMC %s: configuration staged, will be applied at next resume", obj->name);
  }
  return JIM_OK;
}

// TODO Add cget like function
static const struct command_registration tmc_instance_command_handlers[] = {
    {
        .name = "init",
        .mode = COMMAND_EXEC,
        .help = "",
        .usage = "Initialize TMC configuration",
        .handler = tmc_instance_init_handler,
    },
    {
        .name = "enable",
        .mode = COMMAND_EXEC,
        .help = "",
        .usage = "Enables trace capture",
        .handler = tmc_enable_handler,
    },
    {
        .name = "disable",
        .mode = COMMAND_EXEC,
        .help = "",
        .usage = "Disables trace capture",
        .handler = tmc_disable_handler,
    },
    {
        .name = "trace-dump",
        .mode = COMMAND_EXEC,
        .help = "Writes each captured trace session to a separate binary file based on "
                "<filename>. A session is the trace recorded between two halts.",
        .usage = "<filename>",
        .handler = tmc_trace_dump_handler,
    },
    {
        .name = "configure",
        .mode = COMMAND_EXEC,
        .help = "",
        .usage = "Configures TMC parameters",
        .jim_handler = jim_tmc_configure,
    },
    COMMAND_REGISTRATION_DONE,
};

static int tmc_create(struct jim_getopt_info *goi) {
  Jim_Interp *interp = goi->interp;
  struct tmc_object *obj = calloc(1, sizeof(*obj));
  if (!obj) {
    Jim_SetResultString(interp, "out of memory", -1);
    return JIM_ERR;
  }

  Jim_Obj *name_obj;
  if (jim_getopt_obj(goi, &name_obj) != JIM_OK)
    return JIM_ERR;
  const char *name = Jim_GetString(name_obj, NULL);

  struct tmc_object *existing;
  list_for_each_entry(existing, &all_tmc, lh) {
    if (!strcmp(name, existing->name)) {
      Jim_SetResultFormatted(interp, "TMC '%s' already exists", name);
      return JIM_ERR;
    }
  }
  if (Jim_GetCommand(interp, name_obj, JIM_NONE)) {
    Jim_SetResultFormatted(interp, "command '%s' already exists", name);
    return JIM_ERR;
  }
  obj->name = strdup(name);
  if (!obj->name) {
    free(obj);
    Jim_SetResultString(interp, "out of memory", -1);
    return JIM_ERR;
  }

  tmc_buff_init(obj);
  adiv5_mem_ap_spot_init(&obj->spot);
  obj->initialised = false;
  obj->state = TMC_DISABLED;

  goi->isconfigure = 1;
  if(tmc_stage_config(obj, goi) != JIM_OK) {
    Jim_SetResultString(goi->interp,
                        "Unable to configure TMC", -1);
      free(obj->name);
      free(obj);
      return JIM_ERR;
  }

  if (!obj->ap) {
    Jim_SetResultString(goi->interp,
                        "Unable to configure DAP for TMC", -1);
    free(obj->name);
    free(obj);
    return JIM_ERR;
  }

  const struct command_registration tmc_instance_commands[] = {
      {
          .name = obj->name,
          .mode = COMMAND_ANY,
          .help = "TMC instance command group",
          .usage = "",
          .chain = tmc_instance_command_handlers,
      },
      COMMAND_REGISTRATION_DONE,
  };
  struct command_context *cmd_ctx = current_command_context(interp);
  assert(cmd_ctx);
  LOG_DEBUG("TMC %s: object created", name);

  if (register_commands_with_data(cmd_ctx, NULL, tmc_instance_commands, obj) != ERROR_OK) {
    dap_put_ap(obj->ap);
    free(obj->name);
    free(obj);
    return JIM_ERR;
  }
  target_register_event_callback(tmc_target_callback_event_handler, obj);
  list_add_tail(&obj->lh, &all_tmc);
  return JIM_OK;
}

static int jim_tmc_create(Jim_Interp *interp, int argc, Jim_Obj *const *argv) {
  struct jim_getopt_info goi;
  jim_getopt_setup(&goi, interp, argc - 1, argv + 1);

  if (goi.argc < 1) {
    Jim_WrongNumArgs(interp, 1, argv, "?name? ..options...");
    return JIM_ERR;
  }
  return tmc_create(&goi);
}

COMMAND_HANDLER(handle_tmc_names) {
  struct tmc_object *obj;

  if (CMD_ARGC != 0) {
    return ERROR_COMMAND_SYNTAX_ERROR;
  }

  list_for_each_entry(obj, &all_tmc, lh) command_print(CMD, "%s", obj->name);

  return ERROR_OK;
}

COMMAND_HANDLER(handle_tmc_init) { return tmc_init_all(); }

int tmc_init_all(void) {
  struct tmc_object *obj;
  int retval = ERROR_OK;

  list_for_each_entry(obj, &all_tmc, lh) {
    int r = tmc_instance_init(obj);
    if (r != ERROR_OK) {
      LOG_ERROR("TMC %s: init failed", obj->name);
      if (retval == ERROR_OK)
        retval = r;
    }
  }
  return retval;
}

int tmc_cleanup_all(void) {
  struct tmc_object *obj, *tmp;

  list_for_each_entry_safe(obj, tmp, &all_tmc, lh) {
    tmc_buff_free(obj);
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


static const struct command_registration tmc_subcommand_handlers[] = {
    {.name = "create",
     .mode = COMMAND_ANY,
     .jim_handler = jim_tmc_create,
     .usage = "name [-dap dap] [-ap-num num] [-baseaddr baseaddr] ...",
     .help = "Create a CoreSight TMC object. Implicitly calls configure, so "
             "all options for the configure command are viable"},
    {
        .name = "names",
        .mode = COMMAND_ANY,
        .handler = handle_tmc_names,
        .usage = "",
        .help = "List all registered TMC object names.",
    },
    {
        .name = "init",
        .mode = COMMAND_ANY,
        .handler = handle_tmc_init,
        .usage = "",
        .help = "Initialise all registered TMC objects.",
    },
    COMMAND_REGISTRATION_DONE};

const struct command_registration tmc_command_handlers[] = {
    {
        .name = "tmc",
        .mode = COMMAND_ANY,
        .help = "CoreSight Trace Memory Controller (TMC) commands.",
        .usage = "",
        .chain = tmc_subcommand_handlers,
    },
    COMMAND_REGISTRATION_DONE};

int arm_tmc_register_commands(struct command_context *cmd_ctx) {
  return register_commands(cmd_ctx, NULL, tmc_command_handlers);
}
