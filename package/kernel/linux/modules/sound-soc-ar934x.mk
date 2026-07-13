#
# Copyright (C) 2026 PISEN/WPR003N port to OpenWrt 21.02 / Linux 5.4
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

# sound-soc-ar934x.mk — kmod packages for the ASoC audio subsystem
# vendored under target/linux/ath79/files/sound/soc/ath79/.
#
#  kmod-sound-soc-ath79            : container (always-on glue)
#  kmod-sound-soc-ath79-i2s        : the I2S/SPDIF CPU DAI driver
#  kmod-sound-soc-ath79-mbox       : the MBOX IRQ dispatcher
#  kmod-sound-soc-ath79-pcm        : the PCM platform glue
#  kmod-sound-soc-ath79-internal   : the internal SPDIF/ADC stub
#  kmod-sound-soc-ath79-ak4430     : vendored AKM AK4430 codec (no I2C)
#  kmod-sound-soc-qca-pisen-wpr003n: the ASoC machine driver
#
# All .ko files are produced from target/linux/ath79/files/sound/soc/ath79/
# at kernel build time and live under $(LINUX_DIR)/sound/soc/ath79/ after
# the build.

SOUND_ATH79_FILES := \
	$(LINUX_DIR)/sound/soc/ath79/snd-soc-ath79.ko \
	$(LINUX_DIR)/sound/soc/ath79/snd-soc-ath79-i2s.ko \
	$(LINUX_DIR)/sound/soc/ath79/snd-soc-ath79-mbox.ko \
	$(LINUX_DIR)/sound/soc/ath79/snd-soc-ath79-pcm.ko \
	$(LINUX_DIR)/sound/soc/ath79/snd-soc-ath79-internal-codec.ko \
	$(LINUX_DIR)/sound/soc/ath79/snd-soc-ath79-ak4430.ko \
	$(LINUX_DIR)/sound/soc/ath79/snd-soc-qca-pisen-wpr003n.ko

SOUND_ATH79_AUTOLOAD := \
	snd-soc-ath79 \
	snd-soc-ath79-i2s \
	snd-soc-ath79-mbox \
	snd-soc-ath79-pcm \
	snd-soc-ath79-internal-codec \
	snd-soc-ath79-ak4430 \
	snd-soc-qca-pisen-wpr003n


define KernelPackage/sound-soc-ath79
  SUBMENU:=$(SOUND_MENU)
  TITLE:=Atheros ath79 ASoC glue
  KCONFIG:=CONFIG_SND_SOC_ATH79_SOC
  FILES:=$(LINUX_DIR)/sound/soc/ath79/snd-soc-ath79.ko
  AUTOLOAD:=$(call AutoLoad,55,snd-soc-ath79)
  DEPENDS:=@TARGET_ath79 +kmod-sound-core kmod-sound-soc-core
  $(call AddDepends/sound)
endef

define KernelPackage/sound-soc-ath79/description
 Common glue modules for the Atheros ath79 ASoC audio subsystem
 (audio PLL clock provider).
endef

$(eval $(call KernelPackage,sound-soc-ath79))


define KernelPackage/sound-soc-ath79-i2s
  SUBMENU:=$(SOUND_MENU)
  TITLE:=Atheros ath79 I2S/SPDIF controller
  KCONFIG:=CONFIG_SND_SOC_ATH79_I2S
  FILES:=$(LINUX_DIR)/sound/soc/ath79/snd-soc-ath79-i2s.ko
  AUTOLOAD:=$(call AutoLoad,55,snd-soc-ath79-i2s)
  DEPENDS:=@TARGET_ath79 +kmod-sound-core kmod-sound-soc-core +kmod-sound-soc-ath79
  $(call AddDepends/sound)
endef

define KernelPackage/sound-soc-ath79-i2s/description
 CPU DAI driver for the on-chip I2S / SPDIF block of the
 AR934x SoC family.
endef

$(eval $(call KernelPackage,sound-soc-ath79-i2s))


define KernelPackage/sound-soc-ath79-mbox
  SUBMENU:=$(SOUND_MENU)
  TITLE:=Atheros ath79 DMA MBOX IRQ dispatcher
  KCONFIG:=CONFIG_SND_SOC_ATH79_MBOX
  FILES:=$(LINUX_DIR)/sound/soc/ath79/snd-soc-ath79-mbox.ko
  AUTOLOAD:=$(call AutoLoad,55,snd-soc-ath79-mbox)
  DEPENDS:=@TARGET_ath79 +kmod-sound-core kmod-sound-soc-core
  $(call AddDepends/sound)
endef

define KernelPackage/sound-soc-ath79-mbox/description
 Provides the DMA-end-of-ring IRQ demultiplexer for the ath79
 ASoC audio path.
endef

$(eval $(call KernelPackage,sound-soc-ath79-mbox))


define KernelPackage/sound-soc-ath79-pcm
  SUBMENU:=$(SOUND_MENU)
  TITLE:=Atheros ath79 PCM platform glue
  KCONFIG:=CONFIG_SND_SOC_ATH79_PCM
  FILES:=$(LINUX_DIR)/sound/soc/ath79/snd-soc-ath79-pcm.ko
  AUTOLOAD:=$(call AutoLoad,55,snd-soc-ath79-pcm)
  DEPENDS:=@TARGET_ath79 +kmod-sound-core kmod-sound-soc-core \
	+kmod-sound-soc-ath79 +kmod-sound-soc-ath79-mbox
  $(call AddDepends/sound)
endef

define KernelPackage/sound-soc-ath79-pcm/description
 PCM platform glue driver used to bind I2S controller, AK4430
 codec and DMA ring together.
endef

$(eval $(call KernelPackage,sound-soc-ath79-pcm))


define KernelPackage/sound-soc-ath79-internal
  SUBMENU:=$(SOUND_MENU)
  TITLE:=Atheros ath79 internal SPDIF/ADC codec
  KCONFIG:=CONFIG_SND_SOC_ATH79_INTERNAL_CODEC
  FILES:=$(LINUX_DIR)/sound/soc/ath79/snd-soc-ath79-internal-codec.ko
  AUTOLOAD:=$(call AutoLoad,55,snd-soc-ath79-internal-codec)
  DEPENDS:=@TARGET_ath79 +kmod-sound-core kmod-sound-soc-core
  $(call AddDepends/sound)
endef

define KernelPackage/sound-soc-ath79-internal/description
 Internal SPDIF transmitter / analog ADC stub for the AR934x.
endef

$(eval $(call KernelPackage,sound-soc-ath79-internal))


define KernelPackage/sound-soc-ath79-ak4430
  SUBMENU:=$(SOUND_MENU)
  TITLE:=AKM AK4430 I2S DAC (vendored)
  KCONFIG:=CONFIG_SND_SOC_ATH79_AK4430
  FILES:=$(LINUX_DIR)/sound/soc/ath79/snd-soc-ath79-ak4430.ko
  AUTOLOAD:=$(call AutoLoad,55,snd-soc-ath79-ak4430)
  DEPENDS:=@TARGET_ath79 +kmod-sound-core kmod-sound-soc-core
  $(call AddDepends/sound)
endef

define KernelPackage/sound-soc-ath79-ak4430/description
 AKM AK4430 stereo DAC driver vendored inside the ath79 audio
 subsystem (Linux 5.4 mainline does not include this codec; it
 was merged in v6.11).
endef

$(eval $(call KernelPackage,sound-soc-ath79-ak4430))


define KernelPackage/sound-soc-qca-pisen-wpr003n
  SUBMENU:=$(SOUND_MENU)
  TITLE:=PISEN WPR003N ASoC machine driver
  KCONFIG:=CONFIG_SND_SOC_QCA_PISEN_WPR003N
  FILES:=$(LINUX_DIR)/sound/soc/ath79/snd-soc-qca-pisen-wpr003n.ko
  AUTOLOAD:=$(call AutoLoad,56,snd-soc-qca-pisen-wpr003n)
  DEPENDS:=@TARGET_ath79 \
	+kmod-sound-core kmod-sound-soc-core \
	+kmod-sound-soc-ath79-i2s \
	+kmod-sound-soc-ath79-pcm \
	+kmod-sound-soc-ath79-ak4430
  $(call AddDepends/sound)
endef

define KernelPackage/sound-soc-qca-pisen-wpr003n/description
 Machine driver for the PISEN WPR003N portable router, binding
 the on-chip AR934x I2S controller to the AK4430 codec over the
 3.5 mm jack.
endef

$(eval $(call KernelPackage,sound-soc-qca-pisen-wpr003n))
