/* SPDX-License-Identifier: GPL-2.0-or-later OR MIT */
/*
 * Copyright (C) 2026 PISEN/WPR003N port to Linux 5.4
 *
 * Helpers between the ASoC PCM glue and the mailbox IRQ handler.
 */

#ifndef _ATH79_PCM_H
#define _ATH79_PCM_H

#include <linux/device.h>

struct ath79_mbox;

/*
 * Called from the ARM-style mailbox IRQ to signal the PCM layer
 * that a descriptor has been consumed. The PCM driver pushes the
 * next available buffer to the DMA engine in response.
 */
void ath79_pcm_tx_done(struct ath79_mbox *mb);

#endif
