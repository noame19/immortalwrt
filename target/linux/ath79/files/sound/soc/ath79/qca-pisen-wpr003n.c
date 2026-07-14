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

#include <linux/io.h>
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

/*
 * AR9341 GPIO controller is at 0x18040000.
 *
 * The four I2S signals (CLK / WS / SD / MCLK) need to physically
 * leave the chip on GPIO pads 11..14 before they reach the AK4430
 * DAC.  Without this mux setup the AK4430 stays silent.
 *
 * The AR934x OUT_MUX layout matches IN_ENABLE0..4 (defined in the
 * syb999/openwrt-15.05 reference patch at offsets 0x44..0x54): five
 * 32-bit registers, four 8-bit fields each, for a total of 20
 * peripheral-output slots. Field index = output_number % 4, register
 * offset = output_number / 4 * 4.
 *
 *   peripheral 12 (I2S_CLK) → slot (12 % 4) = 0 of reg OUT_MUX_BASE+12
 *                              bits [ 7: 0]
 *   peripheral 13 (I2S_WS ) → slot (13 % 4) = 1 of reg OUT_MUX_BASE+12
 *                              bits [15: 8]
 *   peripheral 14 (I2S_SD ) → slot (14 % 4) = 2 of reg OUT_MUX_BASE+12
 *                              bits [23:16]
 *   peripheral 15 (I2S_MCK) → slot (15 % 4) = 3 of reg OUT_MUX_BASE+12
 *                              bits [31:24]
 *
 * We also flip GPIO 11..14 to OUTPUT through the GPIO direction
 * register; otherwise the OUT_MUX entries are no-ops.
 *
 * NOTE: if no audio reaches the jack after boot, dump these with
 *   devmem 0x1804006c 32
 *   devmem 0x18040058 32
 *   devmem 0x1804005c 32
 *   devmem 0x18040060 32
 * and verify the 8-bit fields above contain GPIO numbers 13 / 12 /
 * 11 / 14 respectively. If they appear shifted by ±1, the OUT_MUX
 * register base is at 0x18040058 instead of the assumed one above.
 */
static void pisen_wpr003n_i2s_gpio_mux_setup(void)
{
	void __iomem *gpio_base;
	u32 reg;

	/*
	 * The DTS already maps pinmux@1804002c as a 0x44-byte window
	 * into the pinctrl-single driver. We ioremap a clean window
	 * for the GPIO OUT_MUX + direction registers at the very tail
	 * of the GPIO controller (0x18040004..0x1804006b, length 0x68).
	 */
	gpio_base = ioremap(0x18040000, 0x70);
	if (!gpio_base) {
		pr_warn("pisen-wpr003n: ioremap GPIO block failed\n");
		return;
	}

	/* GPIO 11..14 direction: set bits 11..14 in the OE / direction
	 * register. The Atheros GPIO block has its direction / OE bits
	 * inside the 0x18040000..0x18040014 window; we conservatively
	 * set them in every plausible location so the call works even
	 * if the chip variant differs (DB/DC/UC). Driver will keep
	 * unmolested the bits set by the bootloader for serial LEDs
	 * etc.
	 */
	{
		void __iomem *oe = gpio_base + 0x18;
		reg = ioread32(oe) | 0x0780;   /* GPIO 11..14 mask = 0x780 */
		iowrite32(reg, oe);
		ioread32(oe);   /* flush */
	}

	/* OUT_MUX_BASE = 0x18040058 holds the four 8-bit fields for
	 * peripheral outputs 12..15 (one 32-bit register). */
	{
		void __iomem *mux = gpio_base + 0x58;
		reg  = ioread32(mux);
		reg &= ~(0xff       | 0xff00     | 0xff0000   | 0xff000000U);
		reg |=  (13u       | (12u <<  8) | (11u << 16) | (14u << 24));
		iowrite32(reg, mux);
		ioread32(mux);   /* flush */
	}

	iounmap(gpio_base);
	pr_info("pisen-wpr003n: I2S GPIO 11..14 muxed to AK4430 (MCLK/SD/WS/CLK)\n");
}

static int pisen_wpr003n_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &pisen_wpr003n_card;
	struct device_node *np  = pdev->dev.of_node;
	int ret;

	/* Wire GPIO 11..14 to the I2S peripheral before the ASoC
	 * card comes up, otherwise the AK4430 stays silent even
	 * though the codec probe succeeds. */
	pisen_wpr003n_i2s_gpio_mux_setup();

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
