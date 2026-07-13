/* SPDX-License-Identifier: GPL-2.0-or-later OR MIT */
/*
 * Copyright (C) 2026 PISEN/WPR003N port to Linux 5.4
 *
 * Originally derived from syb999/openwrt-15.05 sound/soc/ath79,
 * heavily reworked for the ASoC component framework used in
 * Linux 5.4.
 *
 * Definitions shared between ath79-i2s.c and qca-pisen-wpr003n.c.
 */

#ifndef _ATH79_I2S_H
#define _ATH79_I2S_H

#include <sound/soc.h>

/*
 * The name of the I2S CPU DAI. Machine drivers reference this exact
 * string from their DAI link.
 */
#define ATH79_I2S_DAI_NAME	"ath79-i2s"

/*
 * Operating modes: the AR934x STEREO block can act as a plain I2S
 * master to an external codec, or as an SPDIF transmitter.
 */
enum ath79_i2s_mode {
	ATH79_I2S_MODE_I2S = 0,
	ATH79_I2S_MODE_SPDIF,
};

/*
 * Public accessor for the regmap of the I2S sample-rate / control
 * registers. Used by the machine driver to set hardware parameters
 * when the application layer opens the PCM stream.
 */
struct regmap *ath79_i2s_get_regmap(struct device *dev);

#endif /* _ATH79_I2S_H */
