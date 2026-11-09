// SPDX-License-Identifier: GPL-2.0-only
/* Initial rosco_m68k R2 console and timer bring-up. */

#include <linux/clocksource.h>
#include <linux/console.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>
#include <linux/platform_data/serial-sccnxp.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/spi/mmc_spi.h>
#include <linux/spi/spi_bitbang.h>
#include <asm/machdep.h>
#include <asm/traps.h>

#define ROSCO_DUART	0xf00001
#define ROSCO_TIMER_IRQ	5
#define ROSCO_TIMER_BIT	0x08
#define ROSCO_TIMER_RELOAD (3686400 / (16 * 2 * HZ))
#define ROSCO_SD_CS	BIT(2)
#define ROSCO_SD_CLK	BIT(4)
#define ROSCO_SD_MOSI	BIT(6)
#define ROSCO_SD_MISO	BIT(2)

static u8 rosco_uart_imr;
static bool rosco_irq_enabled;

asmlinkage void system_call(void);
asmlinkage void buserr(void);
asmlinkage void trap(void);
asmlinkage void bad_interrupt(void);
asmlinkage void rosco_inthandler(void);

static u8 duart_read(unsigned int reg)
{
	return readb((void __iomem *)(ROSCO_DUART + 2 * reg));
}

static void duart_write(unsigned int reg, u8 value)
{
	writeb(value, (void __iomem *)(ROSCO_DUART + 2 * reg));
}

static void rosco_putchar(char c)
{
	while (!(duart_read(1) & 0x04))
		cpu_relax();
	duart_write(3, c);
}

static void rosco_console_write(struct console *con, const char *s,
				unsigned int count)
{
	while (count--) {
		if (*s == '\n')
			rosco_putchar('\r');
		rosco_putchar(*s++);
	}
}

static struct console rosco_console = {
	.name = "earlyrosco",
	.write = rosco_console_write,
	.flags = CON_PRINTBUFFER | CON_BOOT,
	.index = -1,
};

static void rosco_irq_mask(struct irq_data *d)
{
	rosco_irq_enabled = false;
	duart_write(5, 0);
}

static void rosco_irq_unmask(struct irq_data *d)
{
	rosco_irq_enabled = true;
	duart_write(5, ROSCO_TIMER_BIT | rosco_uart_imr);
}

static void rosco_uart_write_imr(struct device *dev, u8 mask)
{
	unsigned long flags;

	local_irq_save(flags);
	rosco_uart_imr = mask & 0x33;
	if (rosco_irq_enabled)
		duart_write(5, ROSCO_TIMER_BIT | rosco_uart_imr);
	local_irq_restore(flags);
}

static struct sccnxp_pdata rosco_uart_data = {
	.reg_shift = 1,
	.acr_fixed = 0x70,
	.write_imr = rosco_uart_write_imr,
	.shared_irq = true,
};

static struct resource rosco_uart_resources[] = {
	DEFINE_RES_MEM(ROSCO_DUART, 0x20),
	DEFINE_RES_IRQ(ROSCO_TIMER_IRQ),
};

static struct platform_device rosco_uart_device = {
	.name = "xr68c681",
	.id = -1,
	.resource = rosco_uart_resources,
	.num_resources = ARRAY_SIZE(rosco_uart_resources),
	.dev.platform_data = &rosco_uart_data,
};

#if IS_BUILTIN(CONFIG_SPI_BITBANG)
/* Writes to SOP/ROP control pins; reads of those registers control the timer. */
static void rosco_spi_cs(struct spi_device *spi, int active)
{
	bool high = !active;

	if (spi->mode & SPI_CS_HIGH)
		high = !high;
	duart_write(14, ROSCO_SD_CLK);
	duart_write(high ? 15 : 14, ROSCO_SD_CS);
}

static u32 rosco_spi_word(struct spi_device *spi, unsigned int ns, u32 word,
			  u8 bits, unsigned int flags)
{
	u32 result = 0;

	/* Mode 0, MSB first. IRQs remain enabled throughout the transfer. */
	while (bits--) {
		duart_write(word & BIT(bits) ? 15 : 14, ROSCO_SD_MOSI);
		ndelay(ns);
		duart_write(15, ROSCO_SD_CLK);
		ndelay(ns);
		result = (result << 1) | !!(duart_read(13) & ROSCO_SD_MISO);
		duart_write(14, ROSCO_SD_CLK);
	}
	return result;
}

static int rosco_sd_init(struct device *dev,
			irqreturn_t (*handler)(int, void *), void *data)
{
	struct mmc_host *host = data;

	/* Bound scatterlists and request latency on the 1 MiB bitbanged host. */
	host->max_blk_count = PAGE_SIZE / 512;
	host->max_req_size = PAGE_SIZE;
	host->max_segs = PAGE_SIZE / 512;
	return 0;
}

static struct mmc_spi_platform_data rosco_sd_data = {
	.init = rosco_sd_init,
	.ocr_mask = MMC_VDD_32_33 | MMC_VDD_33_34,
	/* No card-detect wire: insert the card before starting Linux. */
	.caps = MMC_CAP_NONREMOVABLE,
};

static struct spi_board_info rosco_sd_info __initdata = {
	.modalias = "mmc-spi-slot",
	.max_speed_hz = 400000,
	.bus_num = 0,
	.chip_select = 0,
	.mode = SPI_MODE_0,
	.platform_data = &rosco_sd_data,
};

static int __init rosco_spi_init(void)
{
	struct spi_controller *ctlr;
	struct spi_bitbang *bang;
	int ret;

	/* All GPIO outputs are software-controlled. Deselect both board CS pins. */
	duart_write(13, 0);
	duart_write(15, ROSCO_SD_CS | ROSCO_SD_MOSI | BIT(7));
	duart_write(14, ROSCO_SD_CLK);
	ctlr = spi_alloc_host(&rosco_uart_device.dev, sizeof(*bang));
	if (!ctlr)
		return -ENOMEM;
	ctlr->bus_num = 0;
	ctlr->num_chipselect = 1;
	ctlr->mode_bits = SPI_CS_HIGH;
	ctlr->bits_per_word_mask = SPI_BPW_MASK(8);
	ctlr->max_speed_hz = 400000;
	bang = spi_controller_get_devdata(ctlr);
	bang->ctlr = ctlr;
	bang->chipselect = rosco_spi_cs;
	bang->txrx_word[SPI_MODE_0] = rosco_spi_word;
	ret = spi_bitbang_start(bang);
	if (ret) {
		spi_controller_put(ctlr);
		return ret;
	}
	if (!spi_new_device(ctlr, &rosco_sd_info)) {
		spi_bitbang_stop(bang);
		return -ENODEV;
	}
	return 0;
}
#else
static int __init rosco_spi_init(void)
{
	return 0;
}
#endif

static int __init rosco_devices_init(void)
{
	int ret = platform_device_register(&rosco_uart_device);

	if (ret)
		return ret;
	return rosco_spi_init();
}
arch_initcall(rosco_devices_init);

static struct irq_chip rosco_irq_chip = {
	.name = "XR68C681",
	.irq_mask = rosco_irq_mask,
	.irq_unmask = rosco_irq_unmask,
};

void __init init_IRQ(void)
{
	duart_write(5, 0);
	irq_set_chip_and_handler(ROSCO_TIMER_IRQ, &rosco_irq_chip,
				handle_level_irq);
}

void __init trap_init(void)
{
	unsigned int i;

	for (i = 2; i < 256; i++)
		_ramvec[i] = trap;
	for (i = 24; i < 32; i++)
		_ramvec[i] = bad_interrupt;
	_ramvec[VEC_BUSERR] = buserr;
	_ramvec[VEC_SYS] = system_call;
	_ramvec[0x45] = rosco_inthandler;
}

static irqreturn_t rosco_tick(int irq, void *dev)
{
	if (!(duart_read(5) & ROSCO_TIMER_BIT))
		return IRQ_NONE;
	/* In timer mode, STOP clears the interrupt without stopping counting. */
	duart_read(15);
	legacy_timer_tick(1);
	return IRQ_HANDLED;
}

static void __init rosco_timer_init(void)
{
	int ret;

	duart_write(5, 0);
	duart_write(12, 0x45);
	duart_write(4, 0xf0);
	/* 3.6864 MHz crystal, divide by 16, two periods per interrupt. */
	duart_write(6, ROSCO_TIMER_RELOAD >> 8);
	duart_write(7, ROSCO_TIMER_RELOAD & 0xff);
	duart_read(15);
	ret = request_irq(ROSCO_TIMER_IRQ, rosco_tick, IRQF_TIMER | IRQF_SHARED,
			  "rosco-timer", &rosco_irq_chip);
	if (ret)
		panic("Cannot request rosco timer IRQ: %d", ret);
	duart_read(14);
}

static void rosco_reset(void)
{
	local_irq_disable();
	asm volatile ("moveq #0,%%d0\n\t"
		      "movec %%d0,%%vbr\n\t"
		      "move.l 0xe00000,%%sp\n\t"
		      "move.l 0xe00004,%%a0\n\t"
		      "jmp (%%a0)" : : : "d0", "a0", "memory");
}

void __init config_BSP(char *command, int len)
{
	register_console(&rosco_console);
	pr_notice("rosco_m68k R2: MC68010, 1 MiB RAM, split RAM/flash kernel\n");
	mach_sched_init = rosco_timer_init;
	mach_reset = rosco_reset;
}
