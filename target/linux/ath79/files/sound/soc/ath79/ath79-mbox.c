/* SPDX-License-Identifier: GPL-2.0-or-later OR MIT */
/*
 * Copyright (C) 2026 PISEN/WPR003N port to Linux 5.4
 *
 * Originally derived from syb999/openwrt-15.05 ath79-mbox.c, ported
 * to the modern device-tree / devm_irq / regmap idiom for Linux 5.4
 * and expanded with the descriptor-pool management that the original
 * 3.18 driver exposed via ath79-mbox.c helpers.
 *
 * This driver owns two responsibilities:
 *
 *   1. The MBOX DMA-completion IRQ dispatcher. The single MBOX IRQ
 *      is shared between playback (RX descriptors draining) and
 *      capture (TX descriptors filling). The register status bits
 *      tell us which direction completed; we dispatch to the
 *      matching callback registered by the PCM layer.
 *
 *   2. The DMA descriptor ring. The original platform driver used
 *      a custom descriptor ring with 28-bit physical addresses and
 *      per-period descriptors. We preserve the layout here so the
 *      algorithm stays identical to the reference, only the
 *      register access now goes through the syscon regmap instead
 *      of the removed 3.18 ath79_dma_{rr,wr}() inline helpers.
 *
 * Register offsets and bit definitions come from the original
 * syb999 999-add-sound-ak4430.patch.
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/mfd/syscon.h>

#include <sound/pcm.h>

#include "ath79-mbox.h"

/* ------------------------------------------------------------------
 * Register offsets (relative to 0x180a0000 on the AR934x)
 * ------------------------------------------------------------------ */
#define MBOX_REG_FIFO			0x00
#define MBOX_REG_FIFO_STATUS		0x08
#define MBOX_REG_SLIC_FIFO_STATUS	0x0c
#define MBOX_REG_DMA_POLICY		0x10
#define MBOX_REG_SLIC_DMA_POLICY	0x14
#define MBOX_REG_RX_DESC_BASE		0x18	/* MBOX0 RX descriptor base */
#define MBOX_REG_RX_CONTROL		0x1c	/* MBOX0 RX control */
#define MBOX_REG_TX_DESC_BASE		0x20	/* MBOX0 TX descriptor base */
#define MBOX_REG_TX_CONTROL		0x24	/* MBOX0 TX control */
#define MBOX_REG_FRAME			0x38
#define MBOX_REG_SLIC_FRAME		0x3c
#define MBOX_REG_FIFO_TIMEOUT		0x40
#define MBOX_REG_INT_STATUS		0x44
#define MBOX_REG_SLIC_INT_STATUS	0x48
#define MBOX_REG_INT_ENABLE		0x4c
#define MBOX_REG_SLIC_INT_ENABLE	0x50
#define MBOX_REG_FIFO_RESET		0x58
#define MBOX_REG_SLIC_FIFO_RESET	0x5c

/* DMA control bits (RX_CONTROL / TX_CONTROL) */
#define MBOX_CTRL_RESUME		BIT(2)
#define MBOX_CTRL_START			BIT(1)
#define MBOX_CTRL_STOP			BIT(0)

/* FIFO_RESET bits */
#define MBOX_FIFO_RESET_MBOX1_RX	BIT(3)
#define MBOX_FIFO_RESET_MBOX0_RX	BIT(2)
#define MBOX_FIFO_RESET_MBOX1_TX	BIT(1)
#define MBOX_FIFO_RESET_MBOX0_TX	BIT(0)
#define MBOX_FIFO_RESET_ALL		0xff

/* INT_STATUS / INT_ENABLE bits */
#define MBOX_INT_MBOX1_RX_COMPLETE	BIT(11)
#define MBOX_INT_MBOX0_RX_COMPLETE	BIT(10)
#define MBOX_INT_MBOX1_TX_EOM		BIT(9)
#define MBOX_INT_MBOX0_TX_EOM		BIT(8)
#define MBOX_INT_MBOX1_TX_COMPLETE	BIT(7)
#define MBOX_INT_MBOX0_TX_COMPLETE	BIT(6)

/* DMA_POLICY bits */
#define MBOX_POLICY_TX_QUANTUM		BIT(3)
#define MBOX_POLICY_RX_QUANTUM		BIT(1)
#define MBOX_POLICY_TX_FIFO_THRESH_SHIFT 4
#define MBOX_POLICY_TX_FIFO_THRESH_MASK  0xf

/* Reset controller bit (uses rst@1806001c, line 0) */
#define AR934X_RESET_MBOX		BIT(0)

/* ------------------------------------------------------------------
 * Module-level singletons
 * ------------------------------------------------------------------ */
DEFINE_SPINLOCK(ath79_pcm_lock);
EXPORT_SYMBOL_GPL(ath79_pcm_lock);

static struct ath79_mbox *g_mbox;

struct ath79_pcm_rt_priv *ath79_mbox_prv_playback;
EXPORT_SYMBOL_GPL(ath79_mbox_prv_playback);
struct ath79_pcm_rt_priv *ath79_mbox_prv_capture;
EXPORT_SYMBOL_GPL(ath79_mbox_prv_capture);

/* ------------------------------------------------------------------
 * Descriptor representation
 * ------------------------------------------------------------------ */
#define DESC_FLAG_OWN		BIT(31)
#define DESC_FLAG_EOM		BIT(30)

static inline dma_addr_t desc_to_phys(u32 word)
{
	/* The hardware only looks at the low 28 bits; clear the
	 * reserved 4 high bits so we have a clean physical address. */
	return (dma_addr_t)(word & 0x0fffffff);
}

static inline u32 phys_to_desc(dma_addr_t p)
{
	return (u32)((p & 0x0fffffff) | DESC_FLAG_OWN);
}

/* ------------------------------------------------------------------
 * Driver private state
 * ------------------------------------------------------------------ */
struct ath79_mbox {
	struct device        *dev;
	struct regmap        *regmap;
	struct reset_control *rst;
	int                   irq;
	struct dma_pool      *desc_pool;

	ath79_mbox_notify_t notify_playback;
	void               *playback_data;
	ath79_mbox_notify_t notify_capture;
	void               *capture_data;
};

static struct ath79_mbox *to_mbox(struct device_node *np)
{
	/* The AR934x has exactly one MBOX block. Keep one singleton. */
	if (g_mbox && g_mbox->dev && g_mbox->dev->of_node == np)
		return g_mbox;
	return NULL;
}

struct ath79_mbox *ath79_mbox_get(struct device_node *np)
{
	return to_mbox(np);
}
EXPORT_SYMBOL_GPL(ath79_mbox_get);

int ath79_mbox_set_callback(struct ath79_mbox *mb,
			    int direction,
			    ath79_mbox_notify_t cb,
			    void *data)
{
	unsigned long flags;

	if (!mb)
		return -ENODEV;

	spin_lock_irqsave(&ath79_pcm_lock, flags);
	if (direction == ATH79_STREAM_PLAYBACK) {
		mb->notify_playback = cb;
		mb->playback_data   = data;
	} else {
		mb->notify_capture = cb;
		mb->capture_data   = data;
	}
	spin_unlock_irqrestore(&ath79_pcm_lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(ath79_mbox_set_callback);

void ath79_mbox_dma_ack(struct ath79_mbox *mb, int direction)
{
	if (!mb || !mb->regmap)
		return;

	if (direction == ATH79_STREAM_PLAYBACK) {
		regmap_write(mb->regmap, MBOX_REG_INT_STATUS,
			     MBOX_INT_MBOX0_RX_COMPLETE);
	} else {
		regmap_write(mb->regmap, MBOX_REG_INT_STATUS,
			     MBOX_INT_MBOX0_TX_COMPLETE);
	}
}
EXPORT_SYMBOL_GPL(ath79_mbox_dma_ack);

/* ------------------------------------------------------------------
 * Descriptor-pool management
 * ------------------------------------------------------------------ */
int ath79_mbox_dma_init(struct ath79_mbox *mb, struct device *dma_dev)
{
	if (!mb || !dma_dev)
		return -EINVAL;

	if (mb->desc_pool)
		return 0;

	mb->desc_pool = dma_pool_create("ath79_pcm_desc", dma_dev,
					sizeof(struct ath79_pcm_desc),
					4, 0);
	if (!mb->desc_pool)
		return -ENOMEM;
	return 0;
}
EXPORT_SYMBOL_GPL(ath79_mbox_dma_init);

void ath79_mbox_dma_exit(struct ath79_mbox *mb)
{
	if (!mb || !mb->desc_pool)
		return;
	dma_pool_destroy(mb->desc_pool);
	mb->desc_pool = NULL;
}
EXPORT_SYMBOL_GPL(ath79_mbox_dma_exit);

int ath79_mbox_dma_map(struct ath79_mbox *mb,
		       struct ath79_pcm_rt_priv *rt,
		       dma_addr_t base_addr,
		       int period_bytes,
		       int buffer_bytes)
{
	struct ath79_pcm_desc *desc;
	dma_addr_t desc_phys;
	unsigned int offset = 0;
	int ret = 0;

	if (!mb || !mb->desc_pool || !rt)
		return -EINVAL;

	INIT_LIST_HEAD(&rt->dma_head);
	rt->elapsed_size = 0;
	rt->last_played  = NULL;

	/*
	 * The descriptor word0 packs OWN|EOM in the top 2 bits,
	 * reserved bits 29..24, then a 12-bit size field (bits 23..12)
	 * and a 12-bit length field (bits 11..0). Hardware only supports
	 * descriptors up to 4095 bytes; the platform driver constrains
	 * period_bytes_max to 4095 accordingly.
	 */
#define DESC_SIZE_SHIFT 12
#define DESC_LEN_MASK   0x0fff
#define DESC_SIZE_MASK  0x0fff

	unsigned int this_size;

	spin_lock(&ath79_pcm_lock);
	do {
		desc = dma_pool_alloc(mb->desc_pool, GFP_KERNEL, &desc_phys);
		if (!desc) {
			ret = -ENOMEM;
			break;
		}
		memset(desc, 0, sizeof(*desc));
		desc->phys = desc_phys;

		if (buffer_bytes >= offset + period_bytes)
			this_size = period_bytes;
		else
			this_size = buffer_bytes - offset;

		desc->word0 = DESC_FLAG_OWN |
			      ((this_size & DESC_SIZE_MASK) << DESC_SIZE_SHIFT) |
			      (this_size & DESC_LEN_MASK);
		desc->word1 = (u32)(base_addr + offset);
		desc->word2 = 0;

		list_add_tail(&desc->list, &rt->dma_head);
		offset += this_size;
	} while (offset < buffer_bytes);
#undef DESC_SIZE_SHIFT
#undef DESC_LEN_MASK
#undef DESC_SIZE_MASK

	if (ret == 0) {
		/* Close the ring: every NextPtr points to the
		 * descriptor that follows in the chain, wrapping at
		 * the head. */
		list_for_each_entry(desc, &rt->dma_head, list) {
			struct ath79_pcm_desc *next;

			if (desc->list.next == &rt->dma_head) {
				next = list_first_entry(&rt->dma_head,
						struct ath79_pcm_desc, list);
			} else {
				next = list_next_entry(desc,
						struct ath79_pcm_desc, list);
			}
			desc->word2 = phys_to_desc(next->phys);
		}
	}
	spin_unlock(&ath79_pcm_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(ath79_mbox_dma_map);

void ath79_mbox_dma_unmap(struct ath79_mbox *mb,
			  struct ath79_pcm_rt_priv *rt)
{
	struct ath79_pcm_desc *desc, *n;

	if (!mb || !mb->desc_pool || !rt)
		return;

	spin_lock(&ath79_pcm_lock);
	list_for_each_entry_safe(desc, n, &rt->dma_head, list) {
		list_del(&desc->list);
		dma_pool_free(mb->desc_pool, desc, desc->phys);
	}
	INIT_LIST_HEAD(&rt->dma_head);
	rt->last_played  = NULL;
	rt->elapsed_size = 0;
	spin_unlock(&ath79_pcm_lock);
}
EXPORT_SYMBOL_GPL(ath79_mbox_dma_unmap);

/* ------------------------------------------------------------------
 * IRQ dispatcher
 * ------------------------------------------------------------------ */
static irqreturn_t ath79_mbox_irq(int irq, void *dev_id)
{
	struct ath79_mbox *mb = dev_id;
	u32 status = 0;
	unsigned long flags;
	bool handled = false;
	ath79_mbox_notify_t cb;
	void *cb_data;

	if (!mb || !mb->regmap)
		return IRQ_NONE;

	regmap_read(mb->regmap, MBOX_REG_INT_STATUS, &status);

	if (status & MBOX_INT_MBOX0_RX_COMPLETE) {
		regmap_write(mb->regmap, MBOX_REG_INT_STATUS,
			     MBOX_INT_MBOX0_RX_COMPLETE);

		spin_lock_irqsave(&ath79_pcm_lock, flags);
		cb      = mb->notify_playback;
		cb_data = mb->playback_data;
		spin_unlock_irqrestore(&ath79_pcm_lock, flags);
		if (cb)
			cb(cb_data);
		handled = true;
	}
	if (status & MBOX_INT_MBOX0_TX_COMPLETE) {
		regmap_write(mb->regmap, MBOX_REG_INT_STATUS,
			     MBOX_INT_MBOX0_TX_COMPLETE);

		spin_lock_irqsave(&ath79_pcm_lock, flags);
		cb      = mb->notify_capture;
		cb_data = mb->capture_data;
		spin_unlock_irqrestore(&ath79_pcm_lock, flags);
		if (cb)
			cb(cb_data);
		handled = true;
	}
	return handled ? IRQ_HANDLED : IRQ_NONE;
}

/* ------------------------------------------------------------------
 * Re-arm descriptors that the hardware has consumed
 * ------------------------------------------------------------------ */
unsigned int ath79_mbox_reclaim(struct ath79_pcm_rt_priv *rt)
{
	struct ath79_pcm_desc *desc;
	unsigned int size = 0;

	if (!rt)
		return 0;

	spin_lock(&ath79_pcm_lock);
	list_for_each_entry(desc, &rt->dma_head, list) {
		if (!(desc->word0 & DESC_FLAG_OWN)) {
			desc->word0 |= DESC_FLAG_OWN;
			size += desc->word0 & 0x00ffffff;
		}
	}
	rt->elapsed_size += size;
	spin_unlock(&ath79_pcm_lock);
	return size;
}
EXPORT_SYMBOL_GPL(ath79_mbox_reclaim);

struct ath79_pcm_desc *ath79_mbox_last_played(struct ath79_pcm_rt_priv *rt)
{
	struct ath79_pcm_desc *desc, *prev = NULL;

	if (!rt)
		return NULL;

	spin_lock(&ath79_pcm_lock);
	list_for_each_entry(desc, &rt->dma_head, list) {
		if (desc->word0 & DESC_FLAG_OWN) {
			if (!prev || !(prev->word0 & DESC_FLAG_OWN)) {
				spin_unlock(&ath79_pcm_lock);
				return desc;
			}
		}
		prev = desc;
	}
	spin_unlock(&ath79_pcm_lock);
	return NULL;
}
EXPORT_SYMBOL_GPL(ath79_mbox_last_played);

/* ------------------------------------------------------------------
 * Engine start / stop / prepare
 * ------------------------------------------------------------------ */
void ath79_mbox_dma_reset(struct ath79_mbox *mb)
{
	unsigned int timeout;

	if (!mb || !mb->regmap)
		return;

	/* Assert + de-assert the MBOX reset line if the reset
	 * framework wired us up. */
	if (mb->rst) {
		reset_control_assert(mb->rst);
		udelay(50);
		reset_control_deassert(mb->rst);
	}

	/* Reset all MBOX FIFOs */
	regmap_write(mb->regmap, MBOX_REG_FIFO_RESET, MBOX_FIFO_RESET_ALL);
	for (timeout = 100; timeout; --timeout) {
		u32 v = 0;
		regmap_read(mb->regmap, MBOX_REG_FIFO_RESET, &v);
		if (!(v & MBOX_FIFO_RESET_ALL))
			break;
		udelay(10);
	}

	/* Make sure DMA is fully stopped */
	regmap_write(mb->regmap, MBOX_REG_RX_CONTROL, MBOX_CTRL_STOP);
	regmap_write(mb->regmap, MBOX_REG_TX_CONTROL, MBOX_CTRL_STOP);
	regmap_read(mb->regmap,  MBOX_REG_RX_CONTROL);	/* flush */
	regmap_read(mb->regmap,  MBOX_REG_TX_CONTROL);
}
EXPORT_SYMBOL_GPL(ath79_mbox_dma_reset);

void ath79_mbox_dma_prepare(struct ath79_mbox *mb,
			    struct ath79_pcm_rt_priv *rt)
{
	struct ath79_pcm_desc *first;
	u32 policy;
	int direction;

	if (!mb || !mb->regmap || !rt || list_empty(&rt->dma_head))
		return;

	first    = list_first_entry(&rt->dma_head, struct ath79_pcm_desc, list);
	direction = rt->direction;

	/* RX (playback) and TX (capture) are physically separate DMA
	 * channels. From the DMA engine's perspective playback reads
	 * from memory (RX) and capture writes to memory (TX). */
	if (direction == ATH79_STREAM_PLAYBACK) {
		regmap_read(mb->regmap, MBOX_REG_DMA_POLICY, &policy);
		policy |= MBOX_POLICY_RX_QUANTUM |
			  (6 << MBOX_POLICY_TX_FIFO_THRESH_SHIFT);
		regmap_write(mb->regmap, MBOX_REG_DMA_POLICY, policy);

		regmap_write(mb->regmap, MBOX_REG_RX_DESC_BASE,
			     (u32)first->phys);
		regmap_update_bits(mb->regmap, MBOX_REG_INT_ENABLE,
				   MBOX_INT_MBOX0_RX_COMPLETE,
				   MBOX_INT_MBOX0_RX_COMPLETE);
	} else {
		regmap_read(mb->regmap, MBOX_REG_DMA_POLICY, &policy);
		policy |= MBOX_POLICY_TX_QUANTUM |
			  (6 << MBOX_POLICY_TX_FIFO_THRESH_SHIFT);
		regmap_write(mb->regmap, MBOX_REG_DMA_POLICY, policy);

		regmap_write(mb->regmap, MBOX_REG_TX_DESC_BASE,
			     (u32)first->phys);
		regmap_update_bits(mb->regmap, MBOX_REG_INT_ENABLE,
				   MBOX_INT_MBOX0_TX_COMPLETE,
				   MBOX_INT_MBOX0_TX_COMPLETE);
	}
}
EXPORT_SYMBOL_GPL(ath79_mbox_dma_prepare);

void ath79_mbox_dma_start(struct ath79_mbox *mb,
			  struct ath79_pcm_rt_priv *rt)
{
	if (!mb || !mb->regmap || !rt)
		return;

	if (rt->direction == ATH79_STREAM_PLAYBACK) {
		regmap_write(mb->regmap, MBOX_REG_RX_CONTROL, MBOX_CTRL_START);
		regmap_read(mb->regmap,  MBOX_REG_RX_CONTROL);	/* flush */
	} else {
		regmap_write(mb->regmap, MBOX_REG_TX_CONTROL, MBOX_CTRL_START);
		regmap_read(mb->regmap,  MBOX_REG_TX_CONTROL);
	}
}
EXPORT_SYMBOL_GPL(ath79_mbox_dma_start);

void ath79_mbox_dma_stop(struct ath79_mbox *mb,
			 struct ath79_pcm_rt_priv *rt)
{
	if (!mb || !mb->regmap || !rt)
		return;

	if (rt->direction == ATH79_STREAM_PLAYBACK) {
		regmap_write(mb->regmap, MBOX_REG_RX_CONTROL, MBOX_CTRL_STOP);
		regmap_read(mb->regmap,  MBOX_REG_RX_CONTROL);
	} else {
		regmap_write(mb->regmap, MBOX_REG_TX_CONTROL, MBOX_CTRL_STOP);
		regmap_read(mb->regmap,  MBOX_REG_TX_CONTROL);
	}
	/* Allow the DMA engine at most one period to drain */
	if (rt->delay_time)
		msleep(rt->delay_time);
}
EXPORT_SYMBOL_GPL(ath79_mbox_dma_stop);

/* ------------------------------------------------------------------
 * Probe
 * ------------------------------------------------------------------ */
static int ath79_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct ath79_mbox *mb;
	int ret;

	mb = devm_kzalloc(dev, sizeof(*mb), GFP_KERNEL);
	if (!mb)
		return -ENOMEM;

	mb->dev = dev;
	platform_set_drvdata(pdev, mb);

	/*
	 * The mbox@180a0000 node carries `compatible = "qca,ar9341-mbox",
	 * "syscon"` so the kernel's generic syscon driver registers a
	 * regmap for the same window.  The DTS in this port does NOT
	 * carry a `regmap = <&phandle>` property, so fall through to
	 * syscon_node_to_regmap() to pick up the self-registered one.
	 * -ENODEV means the syscon driver hasn't probed yet → ask the
	 * kernel to retry once it shows up.
	 */
	mb->regmap = syscon_regmap_lookup_by_phandle(np, "regmap");
	if (IS_ERR(mb->regmap))
		mb->regmap = syscon_node_to_regmap(np);
	if (IS_ERR(mb->regmap))
		mb->regmap = device_node_to_regmap(np->parent);
	if (IS_ERR(mb->regmap)) {
		if (PTR_ERR(mb->regmap) == -ENODEV)
			return -EPROBE_DEFER;
		return dev_err_probe(dev, PTR_ERR(mb->regmap),
				     "no MBOX regmap\n");
	}

	mb->irq = platform_get_irq(pdev, 0);
	if (mb->irq <= 0)
		return dev_err_probe(dev, -EINVAL,
				     "MBOX IRQ missing from DTS\n");

	ret = devm_request_irq(dev, mb->irq, ath79_mbox_irq, 0,
			       "ath79-mbox", mb);
	if (ret)
		return dev_err_probe(dev, ret, "request MBOX IRQ failed\n");

	mb->rst = devm_reset_control_get_optional_exclusive(dev, "mbox");
	if (IS_ERR(mb->rst))
		return dev_err_probe(dev, PTR_ERR(mb->rst),
				     "MBOX reset lookup failed\n");

	g_mbox = mb;
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

MODULE_DESCRIPTION("Atheros ath79 AR934x MBOX DMA controller + IRQ dispatcher");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ath79-mbox");
