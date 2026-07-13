// SPDX-License-Identifier: GPL-2.0-or-later OR MIT
/*
 * Copyright (C) 2026 PISEN/WPR003N port to Linux 5.4
 *
 * Audio PLL (Phase-Locked Loop) for the Atheros AR934x.
 *
 * The PLL register block lives in the same SRIF window as the
 * Ethernet PLLs. In Linux 5.4 it is accessed via a child device-tree
 * node compatible with "qca,ar9341-audio-pll", presented to this
 * driver through regmap (provided by the parent syscon node).
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "ath79-i2s-pll.h"

/*
 * Audio PLL configuration registers (offsets inside the PLL block
 * window, which is 0x10 bytes long starting at 0x18116200).
 *
 *  CONFIG_REG  (0x00):
 *     bits 12..14  EXT_DIV
 *     bits  7.. 9  POSTPLLPWD (1 to power down)
 *     bit   5      PLLPWD (1 to power down)
 *     bit   4      BYPASS
 *     bits  0.. 3  REFDIV
 *
 *  MOD_REG   (0x04):
 *     bits 11..28  TGT_DIV_FRAC
 *     bits  1.. 6  TGT_DIV_INT
 */
#define AR934X_PLL_AUDIO_CONFIG_REG	0x00
#define AR934X_PLL_AUDIO_MOD_REG	0x04

#define AR934X_PLL_AUDIO_CONFIG_REFDIV_MASK	0xf
#define AR934X_PLL_AUDIO_CONFIG_BYPASS		BIT(4)
#define AR934X_PLL_AUDIO_CONFIG_PLLPWD		BIT(5)
#define AR934X_PLL_AUDIO_CONFIG_POSTPLLPWD_MASK	(0x7 << 7)
#define AR934X_PLL_AUDIO_CONFIG_EXT_DIV_MASK	(0x7 << 12)
#define AR934X_PLL_AUDIO_MOD_TGT_DIV_INT_MASK	(0x3f << 1)
#define AR934X_PLL_AUDIO_MOD_TGT_DIV_FRAC_MASK	(0x3ffff << 11)

struct ath79_audio_pll {
	struct regmap *regmap;
	struct clk_hw  hw;
};

static inline struct ath79_audio_pll *to_ath79_pll(struct clk_hw *hw)
{
	return container_of(hw, struct ath79_audio_pll, hw);
}

static int ath79_audio_pll_enable(struct clk_hw *hw)
{
	struct ath79_audio_pll *pll = to_ath79_pll(hw);
	u32 reg;

	regmap_read(pll->regmap, AR934X_PLL_AUDIO_CONFIG_REG, &reg);
	reg &= ~AR934X_PLL_AUDIO_CONFIG_PLLPWD;
	regmap_write(pll->regmap, AR934X_PLL_AUDIO_CONFIG_REG, reg);

	return 0;
}

static void ath79_audio_pll_disable(struct clk_hw *hw)
{
	struct ath79_audio_pll *pll = to_ath79_pll(hw);
	u32 reg;

	regmap_read(pll->regmap, AR934X_PLL_AUDIO_CONFIG_REG, &reg);
	reg |= AR934X_PLL_AUDIO_CONFIG_PLLPWD;
	regmap_write(pll->regmap, AR934X_PLL_AUDIO_CONFIG_REG, reg);
}

static unsigned long ath79_audio_pll_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	/*
	 * Real AR9341 audio PLL rate calculation requires the full
	 * integer/fractional divider math. For the WPR003N port we
	 * always use the 48 kHz × 256 = 12.288 MHz configuration
	 * computed in the DTSI (ref_clk=25 MHz, int=49, frac=...).
	 * The board file (mach layer is gone in 5.4) hardcoded the
	 * values; the DAI driver sets the divider directly via regmap.
	 */
	return 12288000;
}

static const struct clk_ops ath79_audio_pll_ops = {
	.enable     = ath79_audio_pll_enable,
	.disable    = ath79_audio_pll_disable,
	.recalc_rate = ath79_audio_pll_recalc_rate,
};

static int ath79_audio_pll_probe(struct platform_device *pdev)
{
	struct ath79_audio_pll *pll;
	struct clk_init_data init;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	pll = devm_kzalloc(&pdev->dev, sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return -ENOMEM;

	pll->regmap = syscon_regmap_lookup_by_phandle(np, "regmap");
	if (IS_ERR(pll->regmap)) {
		/* Fallback: the parent might be the syscon directly. */
		pll->regmap = device_node_to_regmap(np->parent);
		if (IS_ERR(pll->regmap))
			return PTR_ERR(pll->regmap);
	}

	init.name = "ath79-audio-pll";
	init.ops = &ath79_audio_pll_ops;
	init.flags = 0;
	init.parent_names = NULL;
	init.num_parents = 0;

	pll->hw.init = &init;
	platform_set_drvdata(pdev, pll);

	ret = devm_clk_hw_register(&pdev->dev, &pll->hw);
	if (ret)
		return ret;

	ret = devm_of_clk_add_hw_provider(&pdev->dev, of_clk_hw_simple_get,
					  &pll->hw);
	return ret;
}

static const struct of_device_id ath79_audio_pll_of_match[] = {
	{ .compatible = "qca,ar9341-audio-pll" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ath79_audio_pll_of_match);

static struct platform_driver ath79_audio_pll_driver = {
	.driver = {
		.name = "ath79-audio-pll",
		.of_match_table = ath79_audio_pll_of_match,
	},
	.probe = ath79_audio_pll_probe,
};
module_platform_driver(ath79_audio_pll_driver);

MODULE_DESCRIPTION("Atheros ath79 AR934x Audio PLL");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ath79-audio-pll");
