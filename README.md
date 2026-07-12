<img src="https://avatars.githubusercontent.com/u/53193414?s=200&v=4" alt="logo" width="200" height="200" align="right">

# Project ImmortalWrt

ImmortalWrt is a fork of [OpenWrt](https://openwrt.org), with more packages ported, more devices supported, better performance, and special optimizations for mainland China users.<br/>
Compared the official one, we allow to use hacks or non-upstreamable patches / modifications to achieve our purpose. Source from anywhere.

Default login address: http://192.168.1.1 or http://immortalwrt.lan, username: __root__, password: _none_.

## Download
Built firmware images are available for many architectures and come with a package selection to be used as WiFi home router. To quickly find a factory image usable to migrate from a vendor stock firmware to ImmortalWrt, try the *Firmware Selector*.

- [ImmortalWrt Firmware Selector](https://firmware-selector.immortalwrt.org/)

If your device is supported, please follow the **Info** link to see install instructions or consult the support resources listed below.

## Development
To build your own firmware you need a GNU/Linux, BSD or MacOSX system (case sensitive filesystem required). Cygwin is unsupported because of the lack of a case sensitive file system.<br/>

  ### Requirements
  To build with this project, Ubuntu 20.04 LTS is preferred. And you need use the CPU based on AMD64 architecture, with at least 4GB RAM and 25 GB available disk space. Make sure the __Internet__ is accessible.

  The following tools are needed to compile ImmortalWrt, the package names vary between distributions.

  - Here is an example for Ubuntu users:<br/>
    - Method 1:
      <details>
        <summary>Setup dependencies via APT</summary>

        ```bash
        sudo apt update -y
        sudo apt full-upgrade -y
        sudo apt install -y ack antlr3 asciidoc autoconf automake autopoint binutils bison build-essential \
          bzip2 ccache clang clangd cmake cpio curl device-tree-compiler ecj fastjar flex gawk gettext gcc-multilib \
          g++-multilib git gperf haveged help2man intltool lib32gcc-s1 libc6-dev-i386 libelf-dev libglib2.0-dev \
          libgmp3-dev libltdl-dev libmpc-dev libmpfr-dev libncurses5-dev libncursesw5 libncursesw5-dev libreadline-dev \
          libssl-dev libtool lld lldb lrzsz mkisofs msmtp nano ninja-build p7zip p7zip-full patch pkgconf python2.7 \
          python3 python3-pip python3-ply python-docutils qemu-utils re2c rsync scons squashfs-tools subversion swig \
          texinfo uglifyjs upx-ucl unzip vim wget xmlto xxd zlib1g-dev
        ```
      </details>
    - Method 2:
      ```bash
      sudo bash -c 'bash <(curl -s https://build-scripts.immortalwrt.eu.org/init_build_environment.sh)'
      ```

  Note:
  - Do everything as an unprivileged user, not root, without sudo.
  - Using CPUs based on other architectures should be fine to compile ImmortalWrt, but more hacks are needed - No warranty at all.
  - You must __not__ have spaces or non-ascii characters in PATH or in the work folders on the drive.
  - If you're using Windows Subsystem for Linux (or WSL), removing Windows folders from PATH is required, please see [Build system setup WSL](https://openwrt.org/docs/guide-developer/build-system/wsl) documentation.
  - Using macOS as the host build OS is __not__ recommended. No warranty at all. You can get tips from [Build system setup macOS](https://openwrt.org/docs/guide-developer/build-system/buildroot.exigence.macosx) documentation.
  - For more details, please see [Build system setup](https://openwrt.org/docs/guide-developer/build-system/install-buildsystem) documentation.

  ### Quickstart
  1. Run `git clone -b <branch> --single-branch --filter=blob:none https://github.com/immortalwrt/immortalwrt` to clone the source code.
  2. Run `cd immortalwrt` to enter source directory.
  3. Run `./scripts/feeds update -a` to obtain all the latest package definitions defined in feeds.conf / feeds.conf.default
  4. Run `./scripts/feeds install -a` to install symlinks for all obtained packages into package/feeds/
  5. Run `make menuconfig` to select your preferred configuration for the toolchain, target system & firmware packages.
  6. Run `make` to build your firmware. This will download all sources, build the cross-compile toolchain and then cross-compile the GNU/Linux kernel & all chosen applications for your target system.

  ### Related Repositories
  The main repository uses multiple sub-repositories to manage packages of different categories. All packages are installed via the ImmortalWrt package manager called opkg. If you're looking to develop the web interface or port packages to ImmortalWrt, please find the fitting repository below.
  - [LuCI Web Interface](https://github.com/immortalwrt/luci): Modern and modular interface to control the device via a web browser.
  - [ImmortalWrt Packages](https://github.com/immortalwrt/packages): Community repository of ported packages.
  - [OpenWrt Routing](https://github.com/openwrt/routing): Packages specifically focused on (mesh) routing.

## Support Information
For a list of supported devices see the [OpenWrt Hardware Database](https://openwrt.org/supported_devices)
  ### Documentation
  - [Quick Start Guide](https://openwrt.org/docs/guide-quick-start/start)
  - [User Guide](https://openwrt.org/docs/guide-user/start)
  - [Developer Documentation](https://openwrt.org/docs/guide-developer/start)
  - [Technical Reference](https://openwrt.org/docs/techref/start)

  ### Support Community
  - Support Chat: group [@ctcgfw_openwrt_discuss](https://t.me/ctcgfw_openwrt_discuss) on [Telegram](https://telegram.org/).
  - Support Chat: group [#immortalwrt](https://matrix.to/#/#immortalwrt:matrix.org) on [Matrix](https://matrix.org/).

## License
ImmortalWrt is licensed under [GPL-2.0-only](https://spdx.org/licenses/GPL-2.0-only.html).

## Acknowledgements
<table>
  <tr>
    <td><a href="https://dlercloud.com/"><img src="https://user-images.githubusercontent.com/22235437/111103249-f9ec6e00-8588-11eb-9bfc-67cc55574555.png" width="183" height="52" border="0" alt="Dler Cloud"></a></td>
    <td><a href="https://www.jetbrains.com/"><img src="https://resources.jetbrains.com/storage/products/company/brand/logos/jb_square.png" width="120" height="120" border="0" alt="JetBrains Black Box Logo logo"></a></td>
    <td><a href="https://sourceforge.net/"><img src="https://sourceforge.net/sflogo.php?type=17&group_id=3663829" alt="SourceForge" width=200></a></td>
  </tr>
</table>

---

## 本 fork 的增量：PISEN WPR003N 设备支持

> 本 fork 用于支持 **品胜 PISEN WPR003N** 无线路由器在 **ImmortalWrt 21.02**（OpenWrt 21.02，kernel 5.4）上运行。
>
> - 上游：`immortalwrt/immortalwrt` 的 `openwrt-21.02` 分支
> - 本 fork：`noame19/immortalwrt` 的 `openwrt-21.02` 分支
> - 设备：PISEN WPR003N（TP-Link 衍生公版，AR9341 + 16 MB Flash + ar8229 内置交换）
> - 增量：4 个目标平台文件，单一 commit `8c53ae5`

### 设备规格

| 项 | 规格 |
|---|---|
| SoC | Qualcomm Atheros AR9341（MIPS 74Kc） |
| Flash | 16 MB SPI NOR |
| WiFi | 2.4 GHz 802.11b/g/n（ath9k） |
| 以太网 | 1× WAN（GMAC1，1000 M）+ 1× LAN（GMAC0 MII 接 ar8229 内置交换 port 1） |
| USB | EHCI（USB 2.0） |
| LED | 1 颗蓝色电源灯（GPIO 4），兼 WAN 活动闪烁 |
| 按键 | 复位键（GPIO 17）+ RFKill 拨动开关（GPIO 18） |
| 工厂镜像头 | TP-Link v1，`HARDWARE_ID = 0x08410008` |

### 编译

```bash
git clone --branch openwrt-21.02 --depth 1 \
    https://github.com/noame19/immortalwrt.git immortalwrt-build
cd immortalwrt-build
./scripts/feeds update -a
./scripts/feeds install -a
make menuconfig
# Target System  → Qualcomm Atheros ath79
# Subtarget     → Generic
# Target Profile → PISEN_WPR003N
make -j$(nproc)
```

输出：

- `bin/targets/ath79/generic/pisen-wpr003n-squashfs-sysupgrade.bin`（就地升级）
- `bin/targets/ath79/generic/pisen-wpr003n-tplink.bin`（工厂镜像，TP-Link v1 头）

### 已知边界

1. **audio（I2S/SPDIF/AK4430）未移植** — DTS 不含 audio 节点。
2. **factory 头是 TP-Link v1** — `TPLINK_HWID := 0x08410008`，可走标准 TP-Link 工厂刷机流程。
3. **MAC 地址来源是 u-boot 分区 0x1fc00** — eth0 自动 -1，eth1 自动 +1。
4. **kmod USB 包** — 仅 `kmod-usb-core + kmod-usb2`。AR9341 无 OHCI（仅 EHCI）。
5. **`openwrt-21.02` 分支已 EOL** — ImmortalWrt 上游最后 commit `2024-01-03`（`fc9fe90`），本 fork 不会继续同步上游变更。

### 文件来源

- 旧 ar71xx mach：参考 `openwrt-15.05` 的 `target/linux/ar71xx/files/arch/mips/ath79/mach-pisen-wpr003n.c`（逻辑沿袭）
- 新 DTS 模板：`target/linux/ath79/dts/ar9341.dtsi`、`ar934x.dtsi`、现有 pisen 设备 DTS（`ar9341_pisen_wmb001n.dts`）

### 与 `noame19/openwrt-1` 的关系

本 fork 的姊妹 fork：`https://github.com/noame19/openwrt-1`，那是 OpenWrt 21.02 mainline 上的同名端口。两个 fork 的 4 文件改动**字节级一致**，互为镜像（仅默认 README 与 base tools 不同：immwrt 用 `wpad-openssl`、用 `Emortal` 版本字符串）。

### License

本 fork 新增文件遵循 ImmortalWrt 上游一致协议：`SPDX-License-Identifier: GPL-2.0-or-later OR MIT`（DTS 头部已标）。Boot loader 镜像与工厂镜像头仍归各厂商所有，编译产物遵循 OpenWrt 自身 LICENSE。

ImmortalWrt 主项目说明见原 [README](https://github.com/immortalwrt/immortalwrt/blob/openwrt-21.02/README.md)。
