// SPDX-License-Identifier: GPL-2.0-or-later OR MIT
/*
 * Copyright (C) 2026 PISEN/WPR003N port to Linux 5.4
 *
 * Driver for the AR934x internal SPDIF transmitter / analog ADC
 * stub. Originally derived from syb999/openwrt-15.05 ath79-internal-codec.c,
 * rewritten using the ASoC component framework and the de-migrated
 * snd_soc_register_component() / devm_snd_soc_register_component() API.
 *
 * In syb999's reference setup, the AK4430 external I2S DAC handles
 * the headphone jack output, so this stub is loaded only to satisfy
 * the soc-audio card assembly chain. The PCM playback stream does
 * not actually pass through this codec on the WPR003N.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-component.h>

#define DRV_NAME	"ath79-internal-codec"

#define ATH79_INTERNAL_CODEC_RATES \
	(SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)

#define ATH79_INTERNAL_CODEC_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver ath79_internal_codec_dai = {
	.name = "ath79-internal-hifi",
	.playback = {
		.stream_name  = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates        = ATH79_INTERNAL_CODEC_RATES,
		.formats      = ATH79_INTERNAL_CODEC_FORMATS,
	},
	.capture = {
		.stream_name  = "Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates        = ATH79_INTERNAL_CODEC_RATES,
		.formats      = ATH79_INTERNAL_CODEC_FORMATS,
	},
};

static const struct snd_soc_component_driver ath79_internal_codec_cdrv = {
	.name = DRV_NAME,
};

static int ath79_internal_codec_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
					       &ath79_internal_codec_cdrv,
					       &ath79_internal_codec_dai, 1);
}

static const struct of_device_id ath79_internal_codec_of_match[] = {
	{ .compatible = "qca,ar9341-internal-codec" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ath79_internal_codec_of_match);

static struct platform_driver ath79_internal_codec_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = ath79_internal_codec_of_match,
	},
	.probe = ath79_internal_codec_probe,
};
module_platform_driver(ath79_internal_codec_driver);

MODULE_DESCRIPTION("Atheros ath79 internal SPDIF/ADC stub");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ath79-internal-codec");
