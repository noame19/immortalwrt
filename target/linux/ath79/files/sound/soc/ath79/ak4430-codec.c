// SPDX-License-Identifier: GPL-2.0-or-later OR MIT
/*
 * Copyright (C) 2026 PISEN/WPR003N port to Linux 5.4
 *
 * Vendored AKM AK4430 codec driver for the AR934x ASoC subsystem.
 *
 * Originally derived from syb999/openwrt-15.05 patch
 *     999-add-sound-ak4430.patch
 * Updated from the legacy 3.18 ASoC API to the Linux 5.4
 *     snd_soc_register_component() / devm_snd_soc_register_component()
 *     model.
 *
 * The AK4430 is a stereo DAC wired to the AR934x I2S bus. It does
 * not require explicit I2C register writes for the WPR003N default
 * configuration; the chip operates in its hardware-controlled mode
 * (auto-detection of sample-rate family by the LRCK pin and bit
 * depth by the BICK pin).
 *
 * That allows this driver to be standalone (no I2C bus dependency)
 * which matches the design that booted sound successfully in
 * syb999/openwrt-15.05. If finer-grained register control is
 * required later (e.g. to switch to "digital mute" programmatically)
 * the binding can be extended to an optional I2C adapter.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <sound/soc.h>
#include <sound/soc-component.h>

#define DRV_NAME	"ak4430-codec"

#define AK4430_RATES \
	(SNDRV_PCM_RATE_8000  | SNDRV_PCM_RATE_11025 | \
	 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | \
	 SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
	 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | \
	 SNDRV_PCM_RATE_96000)

#define AK4430_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE | \
	 SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S20_3BE | \
	 SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE | \
	 SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE)

static struct snd_soc_dai_driver ak4430_dai = {
	.name = "ak4430-hifi",
	.playback = {
		.stream_name  = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates        = AK4430_RATES,
		.formats      = AK4430_FORMATS,
	},
	.capture = {
		.stream_name  = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates        = AK4430_RATES,
		.formats      = AK4430_FORMATS,
	},
};

static const struct snd_soc_component_driver ak4430_component = {
	.name = DRV_NAME,
};

static int ak4430_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
					       &ak4430_component,
					       &ak4430_dai, 1);
}

static const struct of_device_id ak4430_of_match[] = {
	{ .compatible = "qca,ak4430" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ak4430_of_match);

static struct platform_driver ak4430_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = ak4430_of_match,
	},
	.probe = ak4430_probe,
};
module_platform_driver(ak4430_driver);

MODULE_DESCRIPTION("ASoC AK4430 DAC driver (HW-mode, no I2C)");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ak4430-codec");
