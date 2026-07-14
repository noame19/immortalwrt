// SPDX-License-Identifier: GPL-2.0-or-later OR MIT
/*
 * Copyright (C) 2026 PISEN/WPR003N port to Linux 5.4
 *
 * ASoC machine driver for the PISEN WPR003N portable router.
 * Derived from the qca-db120-ak4430.c machine driver shipped with
 * syb999/openwrt-15.05, translated to the modern DAILINK_DEFS +
 * snd_soc_card / snd_soc_register_card() idiom used in Linux 5.4.
 *
 * The 3.5 mm jack is driven by an AKM AK4430 DAC wired to the
 * on-chip AR9341 I2S controller via four GPIO lines (11..14).
 * The AK4430 sits on the I2C bus at address 0x10.
 *
 * Hardware setup from syb999/openwrt-15.05 mach-pisen-wpr003n.c:
 *   GPIO 11  I2S SD   (output, mux 14)
 *   GPIO 12  I2S WS   (output, mux 13)
 *   GPIO 13  I2S CLK  (output, mux 12)
 *   GPIO 14  I2S MCLK (output, mux 15)
 *   GPIO 15  SPDIF OUT (output, mux 25) — unused on -DC audio path
 *   GPIO 16  MIC SD   (input, mux 5)
 *
 * The AKM AK4430 is also driven by Linux 5.4's mainline
 * codec driver at sound/soc/codecs/ak4430.c, which is selected
 * via CONFIG_SND_SOC_AK4430.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/soc.h>

#include "ath79-i2s.h"

#define DRV_NAME	"qca-pisen-wpr003n"

SND_SOC_DAILINK_DEFS(pisen,
	DAILINK_COMP_ARRAY(COMP_CPU(ATH79_I2S_DAI_NAME)),
	DAILINK_COMP_ARRAY(COMP_CODEC("ak4430.0-0010", "ak4430-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("ath79-pcm")));

static struct snd_soc_dai_link pisen_wpr003n_dai[] = {
	{
		.name		= "AK4430",
		.stream_name	= "Playback",
		.dai_fmt	= SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBS_CFS,
		SND_SOC_DAILINK_REG(pisen),
	},
};

static struct snd_soc_card pisen_wpr003n_card = {
	.name		= "pisen-wpr003n",
	.owner		= THIS_MODULE,
	.dai_link	= pisen_wpr003n_dai,
	.num_links	= ARRAY_SIZE(pisen_wpr003n_dai),
};

static int pisen_wpr003n_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &pisen_wpr003n_card;
	struct device_node *np  = pdev->dev.of_node;
	int ret;

	card->dev = &pdev->dev;

	if (np) {
		struct snd_soc_dai_link *dai = pisen_wpr003n_dai;

		dai->cpus->of_node = of_parse_phandle(np, "cpu-dai", 0);
		dai->codecs->of_node = of_parse_phandle(np, "codec-dai", 0);
		if (!dai->cpus->of_node || !dai->codecs->of_node) {
			dev_err(&pdev->dev,
				"missing cpu-dai or codec-dai phandle\n");
			return -EINVAL;
		}
	}

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err_probe(&pdev->dev, ret, "register_card failed\n");

	return ret;
}

/*
 * Match table: two rows so the DT's two compatible strings
 * (`"pisen,wpr003n-snd", "qca,ar9341"` in the WPR003N sound node)
 * each get a clean match without falling into the next struct
 * field via designated-initializer tricks.
 */
static const struct of_device_id pisen_wpr003n_of_match[] = {
	{ .compatible = "pisen,wpr003n-snd" },
	{ .compatible = "qca,ar9341" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, pisen_wpr003n_of_match);

static struct platform_driver pisen_wpr003n_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = pisen_wpr003n_of_match,
	},
	.probe = pisen_wpr003n_probe,
};
module_platform_driver(pisen_wpr003n_driver);

MODULE_DESCRIPTION("ASoC machine driver for PISEN WPR003N");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:qca-pisen-wpr003n");
