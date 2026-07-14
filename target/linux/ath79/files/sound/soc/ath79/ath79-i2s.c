// SPDX-License-Identifier: GPL-2.0-or-later OR MIT
/*
 * Copyright (C) 2026 PISEN/WPR003N port to Linux 5.4
 *
 * Originally derived from syb999/openwrt-15.05 ath79-i2s.c (the
 * Atheros 2012/2013 reference driver for the on-chip I2S block of
 * the AR934x family), reworked for the modern ASoC component API.
 *
 * The I2S controller is a relatively small block sitting next to
 * the DMA glue at 0x180B0000 (STereo_BASE on AR71xx/AR934x). It
 * supports both I2S master mode and SPDIF transmitter mode,
 * selected by the second cell of the "mode" property in DTS:
 *
 *   stereo: stereo@180b0000 {
 *       compatible = "qca,ar9341-stereo";
 *       reg = <0x180b0000 0x18>;
 *       mode = <ATH79_I2S_MODE_I2S>;
 *   };
 *
 * The DMA and IRQ resources are described in the device tree and
 * exposed to us through the standard of_node / regmap / irq
 * interfaces.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/mfd/syscon.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-component.h>
#include <sound/dmaengine_pcm.h>

#include "ath79-i2s.h"

/*
 * Register offsets inside the STEREO block. The 0x18-byte window is
 * small enough to enumerate explicitly.
 */
#define AR934X_STEREO_TIMING_REG	0x00
#define AR934X_STEREO_CONFIG_REG	0x04
#define AR934X_STEREO_VOLUME_REG	0x08

/*
 * Bits of the CONFIG register.
 */
#define AR934X_STEREO_CONFIG_SPDIF_ENABLE	BIT(0)
#define AR934X_STEREO_CONFIG_I2S_ENABLE		BIT(1)
#define AR934X_STEREO_CONFIG_MASTER		BIT(2)
#define AR934X_STEREO_CONFIG_BYTESWAP		BIT(3)

/*
 * Supported sample rates and bit depths. Mirrors the qca-db120
 * reference driver.
 */
#define AR934X_I2S_RATES	(SNDRV_PCM_RATE_8000  | \
				 SNDRV_PCM_RATE_11025 | \
				 SNDRV_PCM_RATE_16000 | \
				 SNDRV_PCM_RATE_22050 | \
				 SNDRV_PCM_RATE_32000 | \
				 SNDRV_PCM_RATE_44100 | \
				 SNDRV_PCM_RATE_48000 | \
				 SNDRV_PCM_RATE_88200 | \
				 SNDRV_PCM_RATE_96000)

#define AR934X_I2S_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
				 SNDRV_PCM_FMTBIT_S24_LE | \
				 SNDRV_PCM_FMTBIT_S32_LE)

struct ath79_i2s_priv {
	struct device       *dev;
	struct regmap       *regmap;
	struct reset_control *rst;
	struct clk          *clk;
	int                  irq;
	enum ath79_i2s_mode  mode;
	bool                 running;
};

static int ath79_i2s_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct ath79_i2s_priv *priv = snd_soc_dai_get_drvdata(dai);

	if (!priv->running) {
		reset_control_deassert(priv->rst);
		clk_prepare_enable(priv->clk);
		priv->running = true;
	}
	return 0;
}

static void ath79_i2s_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct ath79_i2s_priv *priv = snd_soc_dai_get_drvdata(dai);

	if (priv->running) {
		clk_disable_unprepare(priv->clk);
		reset_control_assert(priv->rst);
		priv->running = false;
	}
}

static int ath79_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct ath79_i2s_priv *priv = snd_soc_dai_get_drvdata(dai);
	u32 cfg = 0;

	if (priv->mode == ATH79_I2S_MODE_SPDIF)
		cfg |= AR934X_STEREO_CONFIG_SPDIF_ENABLE;
	else
		cfg |= AR934X_STEREO_CONFIG_I2S_ENABLE | AR934X_STEREO_CONFIG_MASTER;

	regmap_update_bits(priv->regmap, AR934X_STEREO_CONFIG_REG,
			   AR934X_STEREO_CONFIG_SPDIF_ENABLE |
			   AR934X_STEREO_CONFIG_I2S_ENABLE |
			   AR934X_STEREO_CONFIG_MASTER,
			   cfg);

	return 0;
}

static const struct snd_soc_dai_ops ath79_i2s_dai_ops = {
	.startup  = ath79_i2s_startup,
	.shutdown = ath79_i2s_shutdown,
	.hw_params = ath79_i2s_hw_params,
};

static struct snd_soc_dai_driver ath79_i2s_dai = {
	.name = ATH79_I2S_DAI_NAME,
	.playback = {
		.stream_name  = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates        = AR934X_I2S_RATES,
		.formats      = AR934X_I2S_FORMATS,
	},
	.capture = {
		.stream_name  = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates        = AR934X_I2S_RATES,
		.formats      = AR934X_I2S_FORMATS,
	},
	.ops = &ath79_i2s_dai_ops,
};

static const struct snd_soc_component_driver ath79_i2s_component = {
	.name = "ath79-i2s",
};

struct regmap *ath79_i2s_get_regmap(struct device *dev)
{
	/*
	 * Public hook: the machine driver can call this to obtain the
	 * regmap of the STEREO block. We just look the priv struct up
	 * via dev_of_node matching the device on which it probed.
	 */
	struct snd_soc_dai *dai = NULL;
	struct ath79_i2s_priv *priv;

	dai = snd_soc_find_dai(ath79_i2s_dai.name);
	if (!dai)
		return ERR_PTR(-ENODEV);

	priv = snd_soc_dai_get_drvdata(dai);
	return priv ? priv->regmap : ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL_GPL(ath79_i2s_get_regmap);

static int ath79_i2s_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct ath79_i2s_priv *priv;
	u32 mode = 0;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	platform_set_drvdata(pdev, priv);

	/*
	 * Register access: three-tier fallback chain because the DTS
	 * pattern depends on whether the board declared us as a
	 * standalone syscon child or as part of a wider syscon window.
	 *
	 *   1. phandle: the node has a `regmap = <&foo>` property that
	 *      points to a separately-declared syscon node (preferred
	 *      on arches like ipq807x).
	 *   2. self:    the node is itself a syscon (its `compatible`
	 *      ends with "syscon"), as on the AR9341 in WPR003N. The
	 *      kernel's "syscon" driver will register a regmap for it
	 *      during its own probe, so on a cold-boot race we may need
	 *      to defer and retry.
	 *   3. parent:  legacy layout where the parent bus holds the
	 *      regmap. Only useful if no syscon at all is involved.
	 */
	priv->regmap = syscon_regmap_lookup_by_phandle(np, "regmap");
	if (IS_ERR(priv->regmap))
		priv->regmap = syscon_node_to_regmap(np);
	if (IS_ERR(priv->regmap))
		priv->regmap = device_node_to_regmap(np->parent);

	if (IS_ERR(priv->regmap)) {
		/* syscon driver hasn't probed yet on a cold boot — ask
		 * the kernel to retry once it shows up. */
		if (PTR_ERR(priv->regmap) == -ENODEV)
			return -EPROBE_DEFER;
		return dev_err_probe(dev, PTR_ERR(priv->regmap),
				     "no regmap available\n");
	}

	if (!of_property_read_u32(np, "mode", &mode))
		priv->mode = mode;

	priv->irq = platform_get_irq(pdev, 0);

	priv->clk = devm_clk_get(dev, "i2s");
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk),
				     "no i2s clock\n");

	priv->rst = devm_reset_control_get_optional_exclusive(dev, "i2s");
	if (IS_ERR(priv->rst))
		return dev_err_probe(dev, PTR_ERR(priv->rst),
				     "no reset control\n");

	ret = devm_snd_soc_register_component(dev, &ath79_i2s_component,
					      &ath79_i2s_dai, 1);
	if (ret)
		return dev_err_probe(dev, ret, "register_component failed\n");

	/*
	 * Stash the priv pointer on the DAI for later accessors.
	 * Component framework does not expose a direct setter, but the
	 * first DAI is at index 0 and we just registered it.
	 */
	snd_soc_dai_set_drvdata(snd_soc_find_dai(ath79_i2s_dai.name), priv);

	return 0;
}

static const struct of_device_id ath79_i2s_of_match[] = {
	{ .compatible = "qca,ar9341-stereo" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ath79_i2s_of_match);

static struct platform_driver ath79_i2s_driver = {
	.driver = {
		.name = "ath79-i2s",
		.of_match_table = ath79_i2s_of_match,
	},
	.probe = ath79_i2s_probe,
};
module_platform_driver(ath79_i2s_driver);

MODULE_DESCRIPTION("Atheros ath79 AR934x I2S / SPDIF controller");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ath79-i2s");
