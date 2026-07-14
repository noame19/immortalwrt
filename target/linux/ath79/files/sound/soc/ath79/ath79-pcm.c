/* SPDX-License-Identifier: GPL-2.0-or-later OR MIT */
/*
 * Copyright (C) 2026 PISEN/WPR003N port to Linux 5.4
 *
 * ASoC PCM platform glue for the Atheros ath79 AR934x audio path.
 * Originally derived from syb999/openwrt-15.05 ath79-pcm.c and
 * ath79-mbox.c, ported to the modern ASoC component framework used
 * by Linux 5.4 (snd_soc_register_component + pcm_construct/destruct
 * + devm_*).
 *
 * Driver split
 *   - ath79-mbox.c  owns the MBOX / DMA-controller register access.
 *   - This driver   builds the per-substream descriptor ring, hooks
 *                   it up to the MBOX, and implements the ALSA PCM
 *                   callbacks (open / close / hw_params / prepare /
 *                   trigger / pointer / pcm_construct / pcm_destruct).
 *
 * On open() we allocate an `ath79_pcm_rt_priv` for the substream,
 * register a notification callback with the MBOX driver, and stash
 * the substream pointer so the MBOX IRQ can call
 * `snd_pcm_period_elapsed()`. On close() we tear down the same.
 *
 * The descriptor ring is built in `hw_params` from the
 * `period_bytes` / `buffer_bytes` negotiated by the ALSA layer and
 * written into the MBOX DMA engine in `prepare`. The DMA engine then
 * raises an IRQ at end-of-ring, the MBOX dispatcher forwards it
 * here, and we signal the framework to consume the next chunk.
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-component.h>

#include "ath79-mbox.h"

#define DRV_NAME	"ath79-pcm"

#define BUFFER_BYTES_MAX	(16 * 4095 * 16)
#define PERIOD_BYTES_MIN	64

static const struct snd_pcm_hardware ath79_pcm_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,
	.formats = SNDRV_PCM_FMTBIT_S8 |
		   SNDRV_PCM_FMTBIT_S16_BE | SNDRV_PCM_FMTBIT_S16_LE |
		   SNDRV_PCM_FMTBIT_S24_BE | SNDRV_PCM_FMTBIT_S24_LE |
		   SNDRV_PCM_FMTBIT_S32_BE | SNDRV_PCM_FMTBIT_S32_LE,
	.rates = SNDRV_PCM_RATE_22050 |
		 SNDRV_PCM_RATE_32000 |
		 SNDRV_PCM_RATE_44100 |
		 SNDRV_PCM_RATE_48000 |
		 SNDRV_PCM_RATE_88200 |
		 SNDRV_PCM_RATE_96000,
	.rate_min = 22050,
	.rate_max = 96000,
	.channels_min = 2,
	.channels_max = 2,

	.buffer_bytes_max = BUFFER_BYTES_MAX,
	.period_bytes_min = PERIOD_BYTES_MIN,
	.period_bytes_max = 4095,	/* = AR934x descriptor size limit */
	.periods_min      = 16,
	.periods_max      = 256,
	.fifo_size        = 0,
};

/* ------------------------------------------------------------------
 * IRQ bottom-half: called from the MBOX dispatcher
 * ------------------------------------------------------------------ */
static void ath79_pcm_period_elapsed(void *data)
{
	struct ath79_pcm_rt_priv *rt = data;
	struct snd_pcm_substream *ss;
	unsigned int period_bytes;
	unsigned int played_bytes;

	if (!rt || !rt->substream)
		return;

	ss = rt->substream;

	/* Walk the descriptor ring and re-OWN any descriptor whose
	 * hardware-OWN bit has been cleared by the DMA engine. */
	played_bytes = ath79_mbox_reclaim(rt);

	/* Update last-played pointer so .pointer() can compute the
	 * current stream position. */
	rt->last_played = ath79_mbox_last_played(rt);

	period_bytes = snd_pcm_lib_period_bytes(ss);
	if (period_bytes && played_bytes > period_bytes)
		dev_dbg(ss->pcm->card->dev,
			"ath79: played %u > period %u\n",
			played_bytes, period_bytes);

	rt->elapsed_size += played_bytes;

	/* Acknowledge the interrupt we just serviced. */
	ath79_mbox_dma_ack(ath79_mbox_get(NULL), rt->direction);

	if (rt->direction == ATH79_STREAM_PLAYBACK) {
		if (rt->elapsed_size >= period_bytes) {
			rt->elapsed_size %= period_bytes;
			snd_pcm_period_elapsed(ss);
		}
	} else {
		if (rt->last_played == NULL) {
			dev_dbg(ss->pcm->card->dev,
				"BUG: ISR but no played buf\n");
			return;
		}
		snd_pcm_period_elapsed(ss);
	}
}

/* ------------------------------------------------------------------
 * Component callbacks
 * ------------------------------------------------------------------ */
static int ath79_pcm_open(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ath79_pcm_rt_priv *rt;
	int err;
	int direction;
	struct ath79_mbox *mb;

	rt = kmalloc(sizeof(*rt), GFP_KERNEL);
	if (!rt)
		return -ENOMEM;

	INIT_LIST_HEAD(&rt->dma_head);
	rt->last_played    = NULL;
	rt->elapsed_size   = 0;
	rt->delay_time     = 0;
	rt->substream      = substream;

	direction = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		    ATH79_STREAM_PLAYBACK : ATH79_STREAM_CAPTURE;
	rt->direction = direction;

	/* Register the bottom-half with the MBOX driver so the
	 * DMA-completion IRQ reaches us. If the MBOX driver hasn't
	 * probed yet we defer. */
	mb = ath79_mbox_get(NULL);
	if (!mb)
		return -EPROBE_DEFER;

	err = ath79_mbox_set_callback(mb, direction,
				      ath79_pcm_period_elapsed, rt);
	if (err) {
		kfree(rt);
		return err;
	}

	if (direction == ATH79_STREAM_PLAYBACK)
		ath79_mbox_prv_playback = rt;
	else
		ath79_mbox_prv_capture = rt;

	snd_soc_set_runtime_hwparams(substream, &ath79_pcm_hardware);
	runtime->private_data = rt;
	return 0;
}

static int ath79_pcm_close(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ath79_pcm_rt_priv *rt = runtime->private_data;
	struct ath79_mbox *mb = ath79_mbox_get(NULL);
	int direction;

	if (!rt)
		return 0;

	direction = rt->direction;
	if (mb)
		ath79_mbox_set_callback(mb, direction, NULL, NULL);

	if (direction == ATH79_STREAM_PLAYBACK)
		ath79_mbox_prv_playback = NULL;
	else
		ath79_mbox_prv_capture = NULL;

	runtime->private_data = NULL;
	kfree(rt);
	return 0;
}

static int ath79_pcm_hw_params(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ath79_pcm_rt_priv *rt = runtime->private_data;
	struct ath79_mbox *mb = ath79_mbox_get(NULL);
	unsigned int period_bytes, buffer_bytes;
	unsigned int sample_size, sample_rate, channels, frames;
	int ret;

	period_bytes = params_period_bytes(hw_params);
	buffer_bytes = params_buffer_bytes(hw_params);

	ret = ath79_mbox_dma_map(mb, rt, substream->dma_buffer.addr,
				 period_bytes, buffer_bytes);
	if (ret)
		return ret;

	/*
	 * When the DMA engine is disabled mid-frame, it can take up to
	 * one full frame to drain. Add a 10 ms margin for the stop()
	 * path to wait long enough before un-mapping memory.
	 */
	sample_size = snd_pcm_format_size(params_format(hw_params), 1);
	sample_rate = params_rate(hw_params);
	channels    = params_channels(hw_params);
	frames      = period_bytes / (sample_size * channels);
	rt->delay_time = (frames * 1000) / sample_rate + 10;

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = buffer_bytes;
	return 0;
}

static int ath79_pcm_hw_free(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ath79_pcm_rt_priv *rt = runtime->private_data;
	struct ath79_mbox *mb = ath79_mbox_get(NULL);

	if (rt && mb)
		ath79_mbox_dma_unmap(mb, rt);

	snd_pcm_set_runtime_buffer(substream, NULL);
	return 0;
}

static int ath79_pcm_prepare(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ath79_pcm_rt_priv *rt = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct ath79_mbox *mb = ath79_mbox_get(NULL);

	/*
	 * Reset the DMA engine the first time each direction is set up
	 * so the FIFO pointers land on a clean state.
	 */
	if (cpu_dai->active == 1)
		ath79_mbox_dma_reset(mb);

	ath79_mbox_dma_prepare(mb, rt);
	return 0;
}

static int ath79_pcm_trigger(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ath79_pcm_rt_priv *rt = runtime->private_data;
	struct ath79_mbox *mb = ath79_mbox_get(NULL);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		ath79_mbox_dma_start(mb, rt);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		ath79_mbox_dma_stop(mb, rt);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static snd_pcm_uframes_t ath79_pcm_pointer(struct snd_soc_component *component,
					   struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ath79_pcm_rt_priv *rt = runtime->private_data;
	struct ath79_pcm_desc *desc;
	snd_pcm_uframes_t pos = 0;

	desc = rt->last_played;
	if (desc) {
		/* BufPtr is the 28-bit physical address inside the
		 * runtime's DMA region; subtract the base address
		 * of the ALSA buffer to get a byte offset. */
		u32 bufptr = desc->word1 & 0x0fffffff;
		pos = bufptr - substream->dma_buffer.addr;
	}
	return bytes_to_frames(runtime, pos);
}

static int ath79_pcm_mmap(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream,
			  struct vm_area_struct *vma)
{
	return remap_pfn_range(vma, vma->vm_start,
			      substream->dma_buffer.addr >> PAGE_SHIFT,
			      vma->vm_end - vma->vm_start,
			      vma->vm_page_prot);
}

/* ------------------------------------------------------------------
 * Component driver
 * ------------------------------------------------------------------ */

/* ------------------------------------------------------------------
 * Construct / destruct: preallocate DMA regions for both streams
 * and lazy-init the descriptor pool against the MBOX driver.
 * ------------------------------------------------------------------ */
static u64 ath79_pcm_dmamask = DMA_BIT_MASK(32);

static int ath79_pcm_new(struct snd_soc_component *component,
			 struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = component->card->snd_card;
	struct device *dev = card->dev;
	int stream, ret;

	if (!dev->dma_mask)
		dev->dma_mask = &ath79_pcm_dmamask;
	if (!dev->coherent_dma_mask)
		dev->coherent_dma_mask = DMA_BIT_MASK(32);

	for (stream = 0; stream < 2; stream++) {
		struct snd_pcm_substream *ss =
			rtd->pcm->streams[stream].substream;
		struct snd_dma_buffer *buf;
		size_t bytes;

		if (!ss)
			continue;

		buf = &ss->dma_buffer;
		bytes = ath79_pcm_hardware.buffer_bytes_max;

		buf->dev.type = SNDRV_DMA_TYPE_DEV;
		buf->dev.dev  = dev;
		buf->private_data = NULL;

		buf->area = dma_alloc_coherent(dev, bytes,
					       &buf->addr, GFP_KERNEL);
		if (!buf->area)
			return -ENOMEM;
		buf->bytes = bytes;
	}

	/* Lazy-create the MBOX descriptor pool so the MBOX driver
	 * has had a chance to bind by now (typical probe order:
	 * mbox probes first because the PCM DT node is a sibling). */
	ret = ath79_mbox_dma_init(ath79_mbox_get(NULL), dev);
	if (ret)
		return ret;

	return 0;
}

static void ath79_pcm_free(struct snd_soc_component *component,
			   struct snd_pcm *pcm)
{
	int stream;

	for (stream = 0; stream < 2; stream++) {
		struct snd_pcm_substream *ss =
			pcm->streams[stream].substream;
		struct snd_dma_buffer *buf;

		if (!ss)
			continue;
		buf = &ss->dma_buffer;
		if (!buf->area)
			continue;
		dma_free_coherent(NULL, buf->bytes, buf->area, buf->addr);
		buf->area = NULL;
	}

	ath79_mbox_dma_exit(ath79_mbox_get(NULL));
}

static const struct snd_soc_component_driver ath79_pcm_component_drv = {
	.name			= DRV_NAME,
	.pcm_construct		= ath79_pcm_new,
	.pcm_destruct		= ath79_pcm_free,
	.open			= ath79_pcm_open,
	.close			= ath79_pcm_close,
	.hw_params		= ath79_pcm_hw_params,
	.hw_free		= ath79_pcm_hw_free,
	.prepare		= ath79_pcm_prepare,
	.trigger		= ath79_pcm_trigger,
	.pointer		= ath79_pcm_pointer,
	.mmap			= ath79_pcm_mmap,
};

/* ------------------------------------------------------------------
 * Platform driver
 * ------------------------------------------------------------------ */
static int ath79_pcm_probe(struct platform_device *pdev)
{
	return snd_soc_register_component(&pdev->dev,
					  &ath79_pcm_component_drv,
					  NULL, 0);
}

static int ath79_pcm_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	return 0;
}

static const struct of_device_id ath79_pcm_of_match[] = {
	{ .compatible = "qca,ar9341-pcm" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ath79_pcm_of_match);

static struct platform_driver ath79_pcm_driver = {
	.driver = {
		.name		= DRV_NAME,
		.of_match_table = ath79_pcm_of_match,
	},
	.probe	= ath79_pcm_probe,
	.remove = ath79_pcm_remove,
};
module_platform_driver(ath79_pcm_driver);

MODULE_DESCRIPTION("Atheros ath79 AR934x ASoC PCM platform glue");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ath79-pcm");
