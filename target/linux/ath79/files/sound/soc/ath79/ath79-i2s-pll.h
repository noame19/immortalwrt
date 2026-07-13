/* SPDX-License-Identifier: GPL-2.0-or-later OR MIT */
/*
 * Copyright (C) 2026 PISEN/WPR003N port to Linux 5.4
 *
 * Originally based on syb999/openwrt-15.05 (Linux 3.18) audio glue.
 *
 * Audio PLL (Phase-Locked Loop) for the Atheros AR934x SoC.
 *
 * The AR934x exposes an audio PLL in the SRIF block at 0x18116200.
 * This driver wraps it as a Linux clock provider so that the I2S
 * controller can ask for an exact sample-rate clock later.
 */

#ifndef _ATH79_I2S_PLL_H
#define _ATH79_I2S_PLL_H

struct device;

int ath79_audio_pll_init(struct device *dev);

#endif
