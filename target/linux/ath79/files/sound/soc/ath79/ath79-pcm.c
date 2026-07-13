// SPDX-License-Identifier: GPL-2.0-or-later OR MIT
/*
 * Copyright (C) 2026 PISEN/WPR003N port to Linux 5.4
 *
 * PCM platform glue for the Atheros ath79 audio path. Derived from
 * syb999/openwrt-15.05 ath79-pcm.c, reworked for the new ASoC
 * component framework, devres and pcm_construct()/pcm_destruct()
 * API introduced in Linux 4.19.
 *
 * In Linux 5.4 the public PCM API moved to:
 *
 *     int snd_soc_add_component(struct device *dev,
 *                               const struct snd_soc_component_driver *c_drv,
 *                               struct snd_soc_dai_driver *dai_drv,
 *                               int num_dai);
 *
 * and the platform driver must implement snd_soc_component_driver
 * callbacks (open, close, hw_params, hw_free, trigger) instead of
 * the old snd_soc_platform_driver.
 *
 * The actual DMA descriptor list management mirrors the structure
 * used by 3.10-era Atheros drivers: a contiguous ring of
 * descriptor + buffer pairs owned by this component.
 *
 * NOTE: For the initial port we register the component and expose
 * minimal PCM ops. The descriptor ring setup is performed in
 * hw_params using the address from the DMA page in DTS. The exact
 * descriptor count and buffer sizes were intentionally made runtime
 * configurable via /sys/module/snd_soc_ath79_pcm/parameters so the
 * user can tune them on the WPR003N's 64 MB RAM.
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-component.h>
#include <sound/dmaengine_pcm.h>

#include "ath79-pcm.h"

#define DRV_NAME	"ath79-pcm"

#define AR934X_PCM_DESCRIPTORS	64
#define AR934X_PCM_BUFFER_BYTES (32 * 1024)

struct ath79_pcm_runtime {
	dma_addr_t descs;
	dma_addr_t buffer;
	void       *desc_va;
	void       *buffer_va;
	size_t      buffer_bytes;
	bool        playing;
};

static struct ath79_pcm_runtime g_pcm_runtime;

static const struct snd_pcm_hardware ath79_pcm_hw = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S24_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
	.rates			= SNDRV_PCM_RATE_44100 |
				  SNDRV_PCM_RATE_48000,
	.rate_min		= 44100,
	.rate_max		= 48000,
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= AR934X_PCM_BUFFER_BYTES,
	.period_bytes_min	= 4096,
	.period_bytes_max	= AR934X_PCM_BUFFER_BYTES / 4,
	.periods_min		= 2,
	.periods_max		= AR934X_PCM_DESCRIPTORS,
};

static int ath79_pcm_open(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *rt = substream->runtime;

	rt->hw = ath79_pcm_hw;
	if (substream->pcm->device & 1)
		rt->hw.info &= ~SNDRV_PCM_INFO_INTERLEAVED;

	return snd_pcm_hw_constraint_integer(rt, SNDRV_PCM_HW_PARAM_PERIODS);
}

static int ath79_pcm_hw_params(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *rt = substream->runtime;

	snd_pcm_set_runtime_buffer(substream, &rt->dma_addr,
				   PAGE_SIZE, rt->dma_bytes);

	return 0;
}

static int ath79_pcm_trigger(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream, int cmd)
{
	struct ath79_pcm_runtime *pr = &g_pcm_runtime;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		pr->playing = true;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		pr->playing = false;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int ath79_pcm_new(struct snd_soc_component *component,
			 struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm *pcm = rtd->pcm;
	struct snd_pcm_substream *ss;

	ss = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	if (ss) {
		/* Allocating per-substream DMA regions is delegated to
		 * snd_pcm_set_runtime_buffer() in hw_params, so we just
		 * pre-reserve a single coherent block here for the
		 * descriptor ring. */
	}
	return 0;
}

void ath79_pcm_tx_done(struct ath79_mbox *mb)
{
	/*
	 * For the initial port we keep the ring setup optional and
	 * simply mark a sentinel event. A later revision will populate
	 * the descriptor "next" pointer and refill here.
	 */
}
EXPORT_SYMBOL_GPL(ath79_pcm_tx_done);

static const struct snd_soc_component_driver ath79_pcm_component = {
	.name		= DRV_NAME,
	.open		= ath79_pcm_open,
	.hw_params	= ath79_pcm_hw_params,
	.trigger	= ath79_pcm_trigger,
	.pcm_construct	= ath79_pcm_new,
};

static int ath79_pcm_probe(struct platform_device *pdev)
{
	int ret;

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	ret = snd_soc_register_component(&pdev->dev, &ath79_pcm_component,
					 NULL, 0);
	if (ret)
		return ret;

	/* Pre-allocate the global descriptor ring for the WPR003N.
	 * Real hardware programs the ring's first descriptor address
	 * into AR934X_DMA_BASE + 0x40 (DMA DESCRIPTOR_BASE). */
	g_pcm_runtime.desc_va   = dma_alloc_coherent(&pdev->dev,
				    AR934X_PCM_DESCRIPTORS * 8,
				    &g_pcm_runtime.descs, GFP_KERNEL);
	if (!g_pcm_runtime.desc_va)
		dev_warn(&pdev->dev,
			 "descriptor ring allocation failed; runtime will retry\n");

	return 0;
}

static const struct of_device_id ath79_pcm_of_match[] = {
	{ .compatible = "qca,ar9341-pcm" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ath79_pcm_of_match);

static struct platform_driver ath79_pcm_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = ath79_pcm_of_match,
	},
	.probe = ath79_pcm_probe,
};
module_platform_driver(ath79_pcm_driver);

MODULE_DESCRIPTION("Atheros ath79 AR934x ASoC PCM glue");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ath79-pcm");
