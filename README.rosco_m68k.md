# Linux on rosco_m68k R2 (MC68010, 1 MiB RAM): proof of concept

This branch is upstream Linux (base `7be4bc52fd9e`, v7.4 merge window) with
a minimal no-MMU port to the [rosco_m68k](https://github.com/rosco-m68k/rosco_m68k)
revision 2 single-board computer, built from the version 2.2 R2 Gerbers.

**Status: proof of concept, verified on real hardware. Not submitted upstream.**

On an MC68010 at 8 MHz with the stock 1 MiB of RAM and a pair of
SST39SF020A flash chips, this kernel:

- boots with working timer interrupts and the scheduler,
- uses the XR68C681 DUART as the serial console (`ttySC0`, 115200 8N1) for
  both input and output,
- detects an SDHC card through the generic SPI-bitbang + `mmc_spi` stack,
  wired to DUART output and input pins,
- mounts a read-only ROMFS root from the card, and
- runs a 356-byte FLAT `/init` as PID 1, which calls `getpid()` and echoes
  lines with `read()`/`write()`.

There is no shell yet. 1 MiB of RAM is very tight, so most kernel text runs
in place from the board's flash. A host-side link step does this split. The
kernel branch does not do it on its own.

Boot log from the board (GNU screen session, unfiltered):

```
Linux version 7.3.0-rosco-02415-g7be4bc52fd9e-dirty (rosco@linux) (m68k-linux-gnu-gcc (GCC) 15.2.0, GNU ld (GNU Binutils) 2.42) #1 2026-10-22 04:53:14 UTC
rosco_m68k R2: MC68010, 1 MiB RAM, split RAM/flash kernel
Kernel command line: console=ttySC0,115200 loglevel=6 panic=0 dhash_entries=128 ihash_entries=128 mhash_entries=64 mphash_entries=64 root=/dev/mmcblk0 \
Kernel command line: rootfstype=romfs ro rootwait init=/init
uart-sccnxp xr68c681: Using default clock frequency
xr68c681: ttySC0 MMIO:0x00f00001 (irq = 5, base_baud = 230400) is a XR68C681
xr68c681: ttySC1 MMIO:0x00f00001 (irq = 5, base_baud = 230400) is a XR68C681
mmc0: host does not support reading read-only switch, assuming write-enable
mmc0: new SDHC card on SPI
mmcblk0: mmc0:0000 TSBBH 3.69 GiB
VFS: Mounted root (romfs filesystem) readonly on device 179:0.
This architecture does not have kernel memory protection.
Run /init as init process
ROSCO Linux userspace from SD; getpid=0x00000001
userspace read: enter a line
Hello, 68010!
userspace received: Hello, 68010!
```

## Commits on this branch

| Commit | Kind | Notes |
| --- | --- | --- |
| `tty: serial: sccnxp: add XR68C681 and shared-IRQ platform hooks` | candidate for upstream | XR68C681 baud table, platform IMR callback, shared IRQ |
| `m68k: add MC68010 CPU and rosco_m68k R2 platform` | candidate, needs work | VBR, format-8 bus-error frames, board file, defconfig |
| `HACK: shrink fixed memory reserves for a 1 MiB NOMMU system` | **not for upstream as is** | Smaller pools/buffers under `BASE_SMALL`, ROMFS-only PoC option |
| `HACK: promote boot milestone messages to NOTICE` | **demo only** | Gives a short boot log at `loglevel=6` |

Only the branch tip has been tested on hardware. It is byte-for-byte the
source of the verified image ("attempt-13"). Intermediate commits are a
logical split and were not built or booted separately.

## Building and booting

The kernel alone is not enough. You also need:

- a GCC 15.2 `m68k-linux-gnu` toolchain patched for strict alignment on
  the 68010 (stock GCC emits odd-address word/long accesses),
- the host-side RAM/flash split linker,
- a small UART loader that programs flash in place and uploads the RAM part
  of the kernel through the stock firmware's Kermit receiver, and
- a ROMFS image with `/init`, written to the SD card.

These are in the companion repository **https://github.com/dmvo/rosco-linux**, together with the
detailed bring-up log and hardware evidence. Check it out next to this tree
as `../linux-rosco`, or set `ROSCO_LINUX_SOURCE`.

```
make ARCH=m68k CROSS_COMPILE=m68k-linux-gnu- \
     KCONFIG_ALLCONFIG=arch/m68k/configs/rosco_m68k_defconfig allnoconfig
make ARCH=m68k CROSS_COMPILE=m68k-linux-gnu- vmlinux
```

## For coding agents

Start with `AGENTS.md` in https://github.com/dmvo/rosco-linux. It has the layout,
exact build commands, the hardware safety rules and the open problems. In
this tree, all rosco-specific code is in the commits listed above
(`git log --stat 7be4bc52fd9e..`). Before preparing anything for
submission, read `Documentation/process/coding-assistants.rst`. AI agents
must not add `Signed-off-by:`.

## Known limitations

- 68010 only. MMU-less, single 1 MiB memory chunk, `HZ=25`.
- The flash execute-in-place split is done by an external script. It is
  not a Kconfig option.
- ROMFS read-only root only. Block writes and direct I/O are turned off by
  `CONFIG_ROSCO_M68K_ROMFS_POC`.
- User-mode faults and signals on the 68010 are implemented but not yet
  tested on hardware.
- No card-detect line, so the card must be inserted before boot.
