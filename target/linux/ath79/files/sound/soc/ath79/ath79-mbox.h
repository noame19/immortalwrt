/* SPDX-License-Identifier: GPL-2.0-or-later OR MIT */
/*
 * Copyright (C) 2026 PISEN/WPR003N port to Linux 5.4
 *
 * Public API exported by ath79-mbox.c to ath79-pcm.c.
 *
 * The MBOX block sits in the DMA / STEREO window at 0x180a0000 on
 * the AR934x. The PCM driver does NOT touch the MBOX / DMA
 * registers itself — it only registers a callback for the MBOX IRQ
 * and asks the MBOX driver to manage the descriptor ring through
 * the helpers below.
 */

#ifndef _ATH79_MBOX_H
#define _ATH79_MBOX_H

#include <linux/device.h>
#include <linux/list.h>
#include <linux/types.h>

struct dma_pool;

/*
 * Descriptor format used by the AR934x MBOX DMA engine.
 *
 * Each descriptor is 4 32-bit words packed by hardware. The PCM
 * layer keeps a parallel software view (list_head + dma_addr_t
 * phys) for bookkeeping.
 *
 *   Word 0   OWN   = bit 31      EOM   = bit 30
 *            rsvd1  = bits 29..24  size  = bits 23..12  (4096-byte aligned)
 *            length = bits 11..0
 *   Word 1   rsvd2  = bits 31..28  BufPtr = bits 27..0  (28-bit phys)
 *   Word 2   rsvd3  = bits 31..28  NextPtr = bits 27..0
 */
struct ath79_pcm_desc {
	u32 word0;
	u32 word1;
	u32 word2;
	u32 word3;

	struct list_head list;
	dma_addr_t       phys;
};

/*
 * Stream direction flag passed to ath79_mbox_* helpers.
 * Matches SNDRV_PCM_STREAM_* values from <sound/pcm.h>.
 */
#define ATH79_STREAM_PLAYBACK	0
#define ATH79_STREAM_CAPTURE	1

/*
 * Per-substream runtime state. Allocated by the PCM driver and
 * pointed at through substream->runtime->private_data.
 *
 * The list of descriptors and the elapsed-size counter are updated
 * by both the PCM and MBOX layers and must be protected by
 * ath79_pcm_lock.
 */
struct ath79_pcm_rt_priv {
	struct list_head        dma_head;
	struct ath79_pcm_desc  *last_played;
	unsigned int            elapsed_size;
	unsigned int            delay_time;
	int                     direction;

	/* Set by the PCM layer on open, so the MBOX IRQ callback can
	 * call snd_pcm_period_elapsed() on the right substream. */
	struct snd_pcm_substream *substream;
};

/*
 * Notification callback: called from the MBOX IRQ handler when a
 * descriptor has been consumed by the DMA engine. The PCM layer
 * uses this to (a) re-arm the OWN bits and (b) signal the ALSA
 * framework that a period has elapsed.
 *
 * `data` is whatever opaque pointer the PCM driver registered.
 */
typedef void (*ath79_mbox_notify_t)(void *data);

/*
 * Look up the singleton MBOX controller that corresponds to this
 * device node. Returns NULL if the MBOX driver has not probed yet.
 */
struct ath79_mbox;
struct ath79_mbox *ath79_mbox_get(struct device_node *np);

/*
 * Register / unregister the playback or capture completion callback.
 * The MBOX driver demuxes RX (playback) and TX (capture) and calls
 * the matching callback.
 */
int ath79_mbox_set_callback(struct ath79_mbox *mb,
			    int direction,
			    ath79_mbox_notify_t cb,
			    void *data);

/*
 * Create / destroy the global descriptor-pool used by all PCM
 * substreams. Called from pcm_construct / pcm_destruct.
 */
int  ath79_mbox_dma_init(struct ath79_mbox *mb, struct device *dma_dev);
void ath79_mbox_dma_exit(struct ath79_mbox *mb);

/*
 * Reset the MBOX DMA engine and FIFOs to a known idle state.
 */
void ath79_mbox_dma_reset(struct ath79_mbox *mb);

/*
 * Build the descriptor ring for a stream:
 *   - Allocate one descriptor per period from the DMA pool
 *   - Point each descriptor at the corresponding sub-range of the
 *     ALSA runtime DMA buffer
 *   - Wire descriptor[i].NextPtr -> descriptor[(i+1) % N]
 *
 * Must be called from pcm_hw_params before ath79_mbox_dma_prepare().
 */
int ath79_mbox_dma_map(struct ath79_mbox    *mb,
		       struct ath79_pcm_rt_priv *rt,
		       dma_addr_t base_addr,
		       int period_bytes,
		       int buffer_bytes);

/*
 * Free the descriptor ring allocated by ath79_mbox_dma_map().
 */
void ath79_mbox_dma_unmap(struct ath79_mbox *mb,
			  struct ath79_pcm_rt_priv *rt);

/*
 * Tell the DMA engine which descriptor ring to use and unmask the
 * matching interrupt. Called from pcm_prepare.
 */
void ath79_mbox_dma_prepare(struct ath79_mbox *mb,
			    struct ath79_pcm_rt_priv *rt);

/*
 * Start / stop the DMA engine for the given stream.
 */
void ath79_mbox_dma_start(struct ath79_mbox *mb,
			  struct ath79_pcm_rt_priv *rt);
void ath79_mbox_dma_stop(struct ath79_mbox *mb,
			 struct ath79_pcm_rt_priv *rt);

/*
 * Acknowledge the matching DMA-completion interrupt bit. Called from
 * the PCM IRQ bottom-half after running the alsa-lib period_elapsed
 * callback.
 */
void ath79_mbox_dma_ack(struct ath79_mbox *mb, int direction);

/*
 * Re-arm OWN bits for descriptors that the hardware has finished
 * draining. Returns the number of bytes re-armed (one period worth
 * per iteration in normal operation).
 */
unsigned int ath79_mbox_reclaim(struct ath79_pcm_rt_priv *rt);

/*
 * Find the descriptor whose BufPtr is between the last-OWN=0 and
 * the current-OWN=0 boundary. Used by the PCM .pointer callback to
 * translate hardware position into an ALSA stream position.
 */
struct ath79_pcm_desc *ath79_mbox_last_played(struct ath79_pcm_rt_priv *rt);

extern struct ath79_pcm_rt_priv *ath79_mbox_prv_playback;
extern struct ath79_pcm_rt_priv *ath79_mbox_prv_capture;

#endif /* _ATH79_MBOX_H */
