// SPDX-License-Identifier: GPL-2.0-or-later OR MIT
/*
 * Copyright (C) 2026 PISEN/WPR003N port to Linux 5.4
 *
 * Derived from syb999/openwrt-15.05 ath79-mbox.c, ported to the
 * modern device-tree / devm_irq / regmap idiom for Linux 5.4.
 *
 * The DMA engine of the AR934x family generates a single interrupt
 * (MBOX_IRQ) when a descriptor reaches its "last" / "end of ring"
 * marker. This driver demultiplexes the interrupt between the
 * playback (STereo block) and capture (internal ADC) pipelines and
 * forwards the events to the PCM layer of snd-soc-ath79-pcm.
 *
 * For the WPR003N we initially only use the playback path, so this
 * driver is wired as a thin producer of one event per DMA end-of-ring.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/mfd/syscon.h>

#include <sound/soc.h>

#include "ath79-i2s.h"

/* MBOX register offsets (inside the 0x180a0000 window) */
#define AR934X_MBOX_DMA_STATUS_REG	0x40
#define AR934X_MBOX_DMA_MASK_REG	0x44
#define AR934X_MBOX_DMA_POLARITY_REG	0x48

#define AR934X_MBOX_DMA_PCM_TX_INT	BIT(0)

struct ath79_mbox_priv {
	struct device  *dev;
	struct regmap  *regmap;
	int             irq;
	void           *pcm_data;     /* opaque pointer to PCM glue */

	/* callback registration filled by the PCM driver */
	void (*notify_tx)(void *pcm_data);
};

static irqreturn_t ath79_mbox_irq(int irq, void *data)
{
	struct ath79_mbox_priv *priv = data;
	u32 status;

	regmap_read(priv->regmap, AR934X_MBOX_DMA_STATUS_REG, &status);
	if (!(status & AR934X_MBOX_DMA_PCM_TX_INT))
		return IRQ_NONE;

	/* Ack the bit by writing 1. */
	regmap_write(priv->regmap, AR934X_MBOX_DMA_STATUS_REG,
		     AR934X_MBOX_DMA_PCM_TX_INT);

	if (priv->notify_tx)
		priv->notify_tx(priv->pcm_data);

	return IRQ_HANDLED;
}

int ath79_mbox_register_callback(struct device *dev,
				 void (*cb)(void *), void *pcm_data)
{
	struct ath79_mbox_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	priv->notify_tx   = cb;
	priv->pcm_data    = pcm_data;
	return 0;
}
EXPORT_SYMBOL_GPL(ath79_mbox_register_callback);

static int ath79_mbox_probe(struct platform_device *pdev)
{
	struct ath79_mbox_priv *priv;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	platform_set_drvdata(pdev, priv);

	/*
	 * The mbox@180a0000 node on the AR9341 declares
	 * `compatible = "qca,ar9341-mbox", "syscon"`, so the kernel's
	 * generic syscon driver registers a regmap for the very same
	 * node. syscon_regmap_lookup_by_phandle() only looks for a
	 * `regmap = <&phandle>` property, which the WPR003N DTS does
	 * not provide, so fall through to syscon_node_to_regmap() to
	 * pick up the self-registered regmap. -EPROBE_DEFER lets the
	 * kernel retry if the syscon driver hasn't probed yet on a
	 * cold boot.
	 */
	priv->regmap = syscon_regmap_lookup_by_phandle(np, "regmap");
	if (IS_ERR(priv->regmap))
		priv->regmap = syscon_node_to_regmap(np);
	if (IS_ERR(priv->regmap))
		priv->regmap = device_node_to_regmap(np->parent);
	if (IS_ERR(priv->regmap)) {
		if (PTR_ERR(priv->regmap) == -ENODEV)
			return -EPROBE_DEFER;
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->regmap),
				     "no regmap\n");
	}

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq <= 0)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "missing IRQ\n");

	ret = devm_request_irq(&pdev->dev, priv->irq, ath79_mbox_irq, 0,
			       "ath79-mbox", priv);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "request_irq failed\n");

	/* Unmask the playback interrupt. */
	regmap_write(priv->regmap, AR934X_MBOX_DMA_MASK_REG,
		     AR934X_MBOX_DMA_PCM_TX_INT);

	return 0;
}

static const struct of_device_id ath79_mbox_of_match[] = {
	{ .compatible = "qca,ar9341-mbox" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ath79_mbox_of_match);

static struct platform_driver ath79_mbox_driver = {
	.driver = {
		.name = "ath79-mbox",
		.of_match_table = ath79_mbox_of_match,
	},
	.probe = ath79_mbox_probe,
};
module_platform_driver(ath79_mbox_driver);

MODULE_DESCRIPTION("Atheros ath79 DMA MBOX IRQ dispatcher");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ath79-mbox");
