// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/math.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/i2c.h>

#define TAG "tc358748: "

#define CHIPID          0x0000
#define SYSCTL          0x0002
#define CONFCTL         0x0004
#define FIFOCTL         0x0006
#define DATAFMT         0x0008
#define PLLCTL0         0x0016
#define PLLCTL1         0x0018
#define CLKCTL          0x0020
#define WORDCNT         0x0022
#define PP_MISC         0x0032
#define STARTCNTRL      0x0204
#define PPISTATUS       0x0208
#define LINEINITCNT     0x0210
#define LPTXTIMECNT     0x0214
#define TCLK_HEADERCNT  0x0218
#define TCLK_TRAILCNT   0x021C
#define THS_HEADERCNT   0x0220
#define TWAKEUP         0x0224
#define TCLK_POSTCNT    0x0228
#define THS_TRAILCNT    0x022C
#define HSTXVREGCNT     0x0230
#define HSTXVREGEN      0x0234
#define TXOPTIONCNTRL   0x0238
#define CSI_CONFW       0x0500
#define CSI_START       0x0518
#define CLW_CNTRL       0x0140
#define D0W_CNTRL       0x0144
#define D1W_CNTRL       0x0148
#define D2W_CNTRL       0x014c
#define D3W_CNTRL       0x0150

#define DBG_LCNT        0x00E0
#define DBG_WIDTH       0x00E2
#define DBG_VBLANK      0x00E4
#define DBG_DATA        0x00E8

/* Values used in the CSI_CONFW register */
#define CSI_SET_REGISTER	(5 << 29)
#define CSI_CLR_REGISTER	(6 << 29)
#define CSI_CONTROL_REG		(3 << 24)





#include <linux/regmap.h>

struct regmap *ctl_regmap;
struct regmap *tx_regmap;

static const struct regmap_range ctl_regmap_rw_ranges[] = {
	regmap_reg_range(0x0000, 0x00ff),
};

static const struct regmap_access_table ctl_regmap_access = {
	.yes_ranges = ctl_regmap_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(ctl_regmap_rw_ranges),
};

static const struct regmap_config ctl_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 2,
	.val_bits = 16,
	.cache_type = REGCACHE_NONE,
	.max_register = 0x00ff,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.rd_table = &ctl_regmap_access,
	.wr_table = &ctl_regmap_access,
	.name = "tc358748-ctl",
};

static const struct regmap_range tx_regmap_rw_ranges[] = {
	regmap_reg_range(0x0100, 0x05ff),
};

static const struct regmap_access_table tx_regmap_access = {
	.yes_ranges = tx_regmap_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(tx_regmap_rw_ranges),
};

static const struct regmap_config tx_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 4,
	.val_bits = 32,
	.cache_type = REGCACHE_NONE,
	.max_register = 0x05ff,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.rd_table = &tx_regmap_access,
	.wr_table = &tx_regmap_access,
	.name = "tc358748-tx",
};







struct i2c_client *tc358748_i2c_client = NULL;

static bool i2c_read(struct i2c_client *client, u8 *send, u16 send_size,
		u8 *receive, u16 receive_size)
{
	int result = 0;

	if (!client)
	{
		pr_err(TAG "I2C client = null");
		return false;
	}

	result = i2c_master_send(client, send, send_size);
	if (result < 0)
	{
		pr_err(TAG "Failed to send I2C the data.\n"); // %.2x
		return false;
	}

	result = i2c_master_recv(client, receive, receive_size);
	if (result < 0)
	{
		pr_err(TAG "Failed to receive I2C the data.\n");
		return false;
	}

	return true;
}

static bool i2c_write(struct i2c_client *client, u8 *send, u16 send_size)
{
	int result = 0;

	if (!client)
	{
		pr_err(TAG "I2C client = null");
		return false;
	}

	result = i2c_master_send(client, send, send_size);
	if (result < 0)
	{
		pr_err(TAG "Failed to send I2C the data.\n"); // %.2x
		return false;
	}

	return true;
}

static bool i2c_write_reg16(struct i2c_client *client, u16 reg, u16 value)
{
	u8 send[4] = { reg >> 8, reg & 0xff, value >> 8, value & 0xff };
	return i2c_write(client, send, sizeof(send));
}

static bool i2c_read_reg16(struct i2c_client *client, u16 reg, u16 *ret_value)
{
	u8 send[2] = { reg >> 8, reg & 0xff };
	u8 receive[2] = { 0 };
	if (!i2c_read(client, send, sizeof(send), receive, sizeof(receive)))
		return false;
	
	*ret_value = (receive[0] << 8) | receive[1];
	return true;
}

static bool i2c_write_reg32(struct i2c_client *client, u16 reg, u32 value)
{
	u8 send[2 + 4] = { reg >> 8, reg & 0xff,
			(value >> 8) & 0xff, value & 0xff, (value >> 24) & 0xff, (value >> 16) & 0xff };
	return i2c_write(client, send, sizeof(send));
}

// static bool i2c_read_reg32(struct i2c_client *client, u16 reg, u32 *ret_value)
// {
// 	u8 send[2] = { reg >> 8, reg & 0xff };
// 	u8 receive[4] = { 0 };
// 	if (!i2c_read(client, send, sizeof(send), receive, sizeof(receive)))
// 		return false;
	
// 	*ret_value = (receive[2] << 24) | (receive[3] << 16) | (receive[0] << 8) | receive[1];
// 	return true;
// }

static unsigned int clk_count(u64 rate, unsigned int ns)
{
	rate *= ns;
	if (do_div(rate, 1000000000))
		rate++; /* Round up the count */
	return rate;
}

static unsigned int clk_ns(unsigned long rate, u64 count)
{
	count *= 1000000000u;
	if (do_div(count, rate))
		count++; /* Round up the time */
	return count;
}

	/* setup PLL in Toshiba TC358748 by I2C */
static bool tc358748_set_pll(void)
{
	/*
			CSI clock dla PCLK = 12'676'060 Hz
		Pixel clock:           800 * 525 * 30,181095238 = 12'676'060 Hz
		Bandwich:              12'676'060 * 24 = 304'225'440 bps
		Data Rate Per Line:    304'225'440 / 4 = 76'056'360 bps
		MIPI D-PHY Clock Rate: 76'056'360 / 2 = 38'028'180 Hz    # / 2 - Double Data Rate (?)

		CSITxClk is obtained by dividing pll_clk by 2.
		Pll_clk = CSITxClk * 2
		Pll_clk = 38'028'180 * 2 = 76'056'360 Hz   # dla DDR - Double Data Rate
		Pll_clk = 76'056'360 * 2 = 152'112'720 Hz  # bez DDR

		In CSI 2 Tx mode, RefClk can be tie to ground.
		In this case, PClk / 4 will be used to drive PLL, Figure 5-3.
		REFCLK = 12'676'060 / 4 = 3'169'015 Hz

		REFCLK * ((FBD + 1) / (PRD + 1)) * (1 / (2 ^ FRS)) = Pll_clk
		3'169'015 * ((383 + 1) / (1 + 1)) * (1 / (2 ^ 3)) =  76'056'360  # z DDR
		3'169'015 * ((383 + 1) / (1 + 1)) * (1 / (2 ^ 2)) = 152'112'720  # bez DDR
		see 'Toshiba PLL calculation.ods'	

		For FRS (HSCK = pll_clk):
		Frequency range setting (post divider) for HSCK frequency
		2’b00: 500MHz – 1GHz HSCK frequency
		2’b01: 250MHz – 500MHz HSCK frequency
		2’b10: 125 MHz – 250MHz HSCK frequency
		2’b11: 62.5MHz – 125MHz HSCK frequency

		example in datasheet:
		16600000 * (255 + 1) / (7 + 1) * (1 / (2 ^ 1)) = 265600000
		16600000 * (319 + 1) / (5 + 1) * (1 / (2 ^ 2)) = 221333333
	*/

	const u16 fbd = 383;
	const u8 prd = 1;
	const u8 frs = 3;      // Pll_clk =  76'056'360   z DDR
	// const u8 frs = 2;   // Pll_clk = 152'112'720 bez DDR  $$
	const u8 sclk_div = frs > 2 ? 2 : frs;
	const u8 clk_div = sclk_div;

	u16 pllctl0;
	u16 pllctl1;
	u16 clkctl;

		/* Setup PLL divider */
	pllctl0 = (prd << 12) | fbd;
	if (!i2c_write_reg16(tc358748_i2c_client, PLLCTL0, pllctl0))
	{
		pr_err(TAG "Can't write PLLCTL0");
		return false;
	}
	pr_info(TAG "PLLCTL0 (0x%04x) = 0x%04x - Setup PLL divider", PLLCTL0, pllctl0);

		/* Start PLL */
	pllctl1 = (frs << 10) |
			(2 << 8) |  /* loop bandwidth 50% */
			(1 << 1) |  /* PLL not reset */
			(1 << 0);   /* PLL enable */
	if (!i2c_write_reg16(tc358748_i2c_client, PLLCTL1, pllctl1))
	{
		pr_err(TAG "Can't write PLLCTL1");
		return false;
	}
	pr_info(TAG "PLLCTL1 (0x%04x) = 0x%04x - Start PLL", PLLCTL1, pllctl1);

		/* Wait for PLL to lock */
	usleep_range(20, 20);
	pr_info(TAG "Wait 20us for PLL to lock");

		/* Clocks dividers */
	clkctl = (clk_div << 4) | (clk_div << 2) | sclk_div;
	if (!i2c_write_reg16(tc358748_i2c_client, CLKCTL, clkctl))
	{
		pr_err(TAG "Can't write CLKCTL");
		return false;
	}
	pr_info(TAG "CLKCTL (0x%04x) = 0x%04x - Setup PLL divider", CLKCTL, clkctl);

		/* Turn on clocks */
	pllctl1 |= 1 << 4;
	if (!i2c_write_reg16(tc358748_i2c_client, PLLCTL1, pllctl1))
	{
		pr_err(TAG "Can't write PLLCTL1");
		return false;
	}
	pr_info(TAG "PLLCTL1 (0x%04x) = 0x%04x - Turn on clocks", PLLCTL1, pllctl1);

	return true;
}

	/* setup Toshiba TC358748 by I2C */
static bool tc358748_setup(void)
{
	u16 chip_id;
	u16 confctl;
	u16 fifoctl;
	u16 datafmt;
	u16 wordcnt;

	const u16 width = 640;
	const u16 height = 480;
	const u16 total_width = 800;
	// const u16 total_height = 525;
	// const u16 h_front_porch = 16;
	// const u16 h_sync = 96;
	// const u16 h_back_porch = 48;
	// const u16 v_front_porch = 10;
	const u16 v_sync = 2;
	// const u16 v_back_porch = 33;

	const u8 bpp = 24;
	const u8 num_data_lanes = 4;
	const u32 pixelclock = 12676060;                      // 800 * 525 * 30,181095238 = 12'676'060
	const u32 csi_bus = 38028180;                         // 38'028'180 z DDR  $$
	// const u32 csi_bus = 76056360;                         // 76'056'360 bez DDR  $$
	const u32 csi_rate = bpp * pixelclock;                // 304'225'440 bps
	const u32 csi_lane_rate = csi_rate / num_data_lanes;  // 76'056'360 (min 62'500'000, max 1G)

	u32 hsbyte_clk;
	u32 linecnt;
	u32 lptxtime;
	u32 t_wakeup;
	u32 tclk_prepare;
	u32 tclk_zero;
	u32 tclk_trail;
	u32 tclk_post;
	u32 ths_prepare;
	u32 ths_zero;
	u32 ths_trail;

	u32 tclk_headercnt;
	u32 ths_headercnt;
	u32 hstxvregcnt;
	u32 hstxvregen;
	u32 csi_confw;
	u16 dbg_cnt;
	u32 dbg_width;
	u16 dbg_vblank;

	pr_info(TAG "  bpp = %d", bpp);
	pr_info(TAG "  num_data_lanes = %d", num_data_lanes);
	pr_info(TAG "  pixelclock = %u", pixelclock);
	pr_info(TAG "  csi_bus = %u", csi_bus);
	pr_info(TAG "  csi_rate = %u", csi_rate);
	pr_info(TAG "  csi_lane_rate = %u", csi_lane_rate);

	if (!i2c_read_reg16(tc358748_i2c_client, CHIPID, &chip_id))
	{
		pr_err(TAG "Can't read ChipId");
		return false;
	}

	if (chip_id != 0x4401)
	{
		pr_err(TAG "Chip not found - ChipId 0x04%x is not 0x4401", chip_id);
		return false;
	}
	pr_info(TAG "ChipId (0x%04x) = 0x%04x - ok", CHIPID, chip_id);

		/* Reset */
	if (!i2c_write_reg16(tc358748_i2c_client, SYSCTL, 1))
	{
		pr_err(TAG "Can't write SYSCTL");
		return false;
	}
	pr_info(TAG "SYSCTL (0x%04x) = 1 - Reset", SYSCTL);

	usleep_range(50 * 1000, 50 * 1000);
	pr_info(TAG "Wait 50ms");

		/* End of reset */
	if (!i2c_write_reg16(tc358748_i2c_client, SYSCTL, 0))
	{
		pr_err(TAG "Can't write SYSCTL");
		return false;
	}
	pr_info(TAG "SYSCTL (0x%04x) = 0 - End of reset", SYSCTL);

		/* setup PLL */
	if (!tc358748_set_pll())
	{
		return false;
	}

		/* CONFCTL */
	confctl = num_data_lanes - 1;
	confctl |=
			(1 << 2) |  /* I2C slave index increment */
			(1 << 3) |  /* Parallel clock polarity inverted - $$ nVidia driver */
			(1 << 4) |  /* H Sync active low */
			(1 << 5) |  /* V Sync active low */
			(1 << 6) |  /* Parallel port enable - $$ nVidia driver */
			// (0 << 8);   /* Parallel data format - mode 0 */
			(3 << 8);   /* Parallel data format - reserved - $$ nVidia driver */
	if (!i2c_write_reg16(tc358748_i2c_client, CONFCTL, confctl))
	{
		pr_err(TAG "Can't write CONFCTL");
		return false;
	}
	pr_info(TAG "CONFCTL (0x%04x) = 0x%04x", CONFCTL, confctl);




// i2c_write_reg16(tc358748_i2c_client, DATAFMT, 0x60);
// i2c_write_reg16(tc358748_i2c_client, CONFCTL, confctl);
// i2c_write_reg16(tc358748_i2c_client, FIFOCTL, 0x20);
// i2c_write_reg16(tc358748_i2c_client, WORDCNT, 0xf00);
// i2c_write_reg32(tc358748_i2c_client, CLW_CNTRL, 0x140);
// i2c_write_reg32(tc358748_i2c_client, D0W_CNTRL, 0x144);
// i2c_write_reg32(tc358748_i2c_client, D1W_CNTRL, 0x148);
// i2c_write_reg32(tc358748_i2c_client, D2W_CNTRL, 0x14c);
// i2c_write_reg32(tc358748_i2c_client, D3W_CNTRL, 0x150);
i2c_write_reg32(tc358748_i2c_client, LINEINITCNT, 0x15ba);
i2c_write_reg32(tc358748_i2c_client, LPTXTIMECNT, 0x2);
i2c_write_reg32(tc358748_i2c_client, TCLK_HEADERCNT, 0xa03);

// i2c_write_reg32(tc358748_i2c_client, TCLK_TRAILCNT, 0xffffffff);
i2c_write_reg32(tc358748_i2c_client, TCLK_TRAILCNT, 1);
// i2c_write_reg32(tc358748_i2c_client, THS_HEADERCNT, 0xffffee03);
// i2c_write_reg32(tc358748_i2c_client, THS_HEADERCNT, 0xffffffff - 0xffffee03);
i2c_write_reg32(tc358748_i2c_client, THS_HEADERCNT, 1);

i2c_write_reg32(tc358748_i2c_client, TWAKEUP, 0x49e0);
i2c_write_reg32(tc358748_i2c_client, TCLK_POSTCNT, 0x7);
i2c_write_reg32(tc358748_i2c_client, THS_TRAILCNT, 0x1);
i2c_write_reg32(tc358748_i2c_client, HSTXVREGEN, 0x1f);
i2c_write_reg32(tc358748_i2c_client, STARTCNTRL, 0x1);
i2c_write_reg32(tc358748_i2c_client, CSI_START, 0x1);
i2c_write_reg32(tc358748_i2c_client, CSI_CONFW, 2734719110);
return true;






		/* FIFOCTL - FiFo level */
	fifoctl = 1; // 12 RGB888 ;//16;  // $$
	if (!i2c_write_reg16(tc358748_i2c_client, FIFOCTL, fifoctl))
	{
		pr_err(TAG "Can't write FIFOCTL");
		return false;
	}
	pr_info(TAG "FIFOCTL (0x%04x) = %d - FiFo Level", FIFOCTL, fifoctl);

		/* DATAFMT - Data Format */
	datafmt = (3 << 4);  /* 3 - RGB888 */
	if (!i2c_write_reg16(tc358748_i2c_client, DATAFMT, datafmt))
	{
		pr_err(TAG "Can't write DATAFMT");
		return false;
	}
	pr_info(TAG "DATAFMT (0x%04x) = 0x%04x - Data Format", DATAFMT, datafmt);

		/* WORDCNT */
	wordcnt = width * bpp / 8;
	if (!i2c_write_reg16(tc358748_i2c_client, WORDCNT, wordcnt))
	{
		pr_err(TAG "Can't write WORDCNT");
		return false;
	}
	pr_info(TAG "WORDCNT (0x%04x) = %d - Word count", WORDCNT, wordcnt);


		/* Compute the D-PHY settings */
	hsbyte_clk = csi_lane_rate / 8;

		/* LINEINITCOUNT >= 100us */
	linecnt = clk_count(hsbyte_clk / 2, 100000);

		/* LPTX clk must be less than 20MHz -> LPTXTIMECNT >= 50 ns */
	lptxtime = clk_count(hsbyte_clk, 50);

		/* TWAKEUP >= 1ms (in LPTX clock count) */
	t_wakeup = clk_count(hsbyte_clk / lptxtime, 1000000);

		/* 38ns <= TCLK_PREPARE <= 95ns */
	tclk_prepare = clk_count(hsbyte_clk, 38);
	if (tclk_prepare > clk_count(hsbyte_clk, 95))
		pr_warn(TAG "TCLK_PREPARE is too long (%u ns)\n",
			clk_ns(hsbyte_clk, tclk_prepare));
	// TODO: Check that TCLK_PREPARE <= 95ns

		/* TCLK_ZERO + TCLK_PREPARE >= 300ns */
	tclk_zero = clk_count(hsbyte_clk, 300) - tclk_prepare;

		/* TCLK_TRAIL >= 60ns */
	tclk_trail = clk_count(hsbyte_clk, 60);

		/* TCLK_POST >= 60ns + 52*UI */
	tclk_post = clk_count(hsbyte_clk, 60 + clk_ns(csi_lane_rate, 52));

		/* 40ns + 4*UI <= THS_PREPARE <= 85ns + 6*UI */
	ths_prepare = clk_count(hsbyte_clk, 40 + clk_ns(csi_lane_rate, 4));
	if (ths_prepare > 85 + clk_ns(csi_lane_rate, 6))
		pr_warn(TAG "THS_PREPARE is too long (%u ns)\n",
			clk_ns(hsbyte_clk, ths_prepare));

		/* THS_ZERO + THS_PREPARE >= 145ns + 10*UI */
	ths_zero = clk_count(hsbyte_clk, 145 +
			clk_ns(csi_lane_rate, 10)) - ths_prepare;

		/* 105ns + 12*UI > THS_TRAIL >= max(8*UI, 60ns + 4*UI) */
	ths_trail = clk_count(hsbyte_clk,
			max(clk_ns(csi_lane_rate, 8),
				60 + clk_ns(csi_lane_rate, 4)));

	pr_info(TAG "  hsbyte_clk = %u", hsbyte_clk);
	pr_info(TAG "  linecnt = %u", linecnt);
	pr_info(TAG "  lptxtime = %u", lptxtime);
	pr_info(TAG "  t_wakeup = %u", t_wakeup);
	pr_info(TAG "  tclk_prepare = %u", tclk_prepare);
	pr_info(TAG "  tclk_zero = %u", tclk_zero);
	pr_info(TAG "  tclk_trail = %u", tclk_trail);
	pr_info(TAG "  tclk_post = %u", tclk_post);
	pr_info(TAG "  ths_prepare = %u", ths_prepare);
	pr_info(TAG "  ths_zero = %u", ths_zero);
	pr_info(TAG "  ths_trail = %u", ths_trail);


		/* Setup D-PHY */
		/* LINEINITCNT */
	if (!i2c_write_reg32(tc358748_i2c_client, LINEINITCNT, linecnt))
	{
		pr_err(TAG "Can't write LINEINITCNT");
		return false;
	}
	pr_info(TAG "LINEINITCNT (0x%04x) = %u", LINEINITCNT, linecnt);

		/* LPTXTIMECNT */
	if (!i2c_write_reg32(tc358748_i2c_client, LPTXTIMECNT, lptxtime))
	{
		pr_err(TAG "Can't write LPTXTIMECNT");
		return false;
	}
	pr_info(TAG "LPTXTIMECNT (0x%04x) = %u", LPTXTIMECNT, lptxtime);

		/* TCLK_HEADERCNT */
	tclk_headercnt = tclk_prepare | (tclk_zero << 8);
	if (!i2c_write_reg32(tc358748_i2c_client, TCLK_HEADERCNT, tclk_headercnt))
	{
		pr_err(TAG "Can't write TCLK_HEADERCNT");
		return false;
	}
	pr_info(TAG "TCLK_HEADERCNT (0x%04x) = 0x%08x", TCLK_HEADERCNT, tclk_headercnt);

		/* TCLK_TRAILCNT */
	if (!i2c_write_reg32(tc358748_i2c_client, TCLK_TRAILCNT, tclk_trail))
	{
		pr_err(TAG "Can't write TCLK_TRAILCNT");
		return false;
	}
	pr_info(TAG "TCLK_TRAILCNT (0x%04x) = %u", TCLK_TRAILCNT, tclk_trail);

		/* THS_HEADERCNT */
	ths_headercnt = ths_prepare | (ths_zero << 8);
	if (!i2c_write_reg32(tc358748_i2c_client, THS_HEADERCNT, ths_headercnt))
	{
		pr_err(TAG "Can't write THS_HEADERCNT");
		return false;
	}
	pr_info(TAG "THS_HEADERCNT (0x%04x) = 0x%08x", THS_HEADERCNT, ths_headercnt);

		/* TWAKEUP */
t_wakeup=1; // $$
	if (!i2c_write_reg32(tc358748_i2c_client, TWAKEUP, t_wakeup))
	{
		pr_err(TAG "Can't write TWAKEUP");
		return false;
	}
	pr_info(TAG "TWAKEUP (0x%04x) = %u", TWAKEUP, t_wakeup);

		/* TCLK_POSTCNT */
	if (!i2c_write_reg32(tc358748_i2c_client, TCLK_POSTCNT, tclk_post))
	{
		pr_err(TAG "Can't write TCLK_POSTCNT");
		return false;
	}
	pr_info(TAG "TCLK_POSTCNT (0x%04x) = %u", TCLK_POSTCNT, tclk_post);

		/* THS_TRAILCNT */
	if (!i2c_write_reg32(tc358748_i2c_client, THS_TRAILCNT, ths_trail))
	{
		pr_err(TAG "Can't write THS_TRAILCNT");
		return false;
	}
	pr_info(TAG "THS_TRAILCNT (0x%04x) = %u", THS_TRAILCNT, ths_trail);

		/* HSTXVREGCNT */
	hstxvregcnt = 5;
	if (!i2c_write_reg32(tc358748_i2c_client, HSTXVREGCNT, hstxvregcnt))
	{
		pr_err(TAG "Can't write HSTXVREGCNT");
		return false;
	}
	pr_info(TAG "HSTXVREGCNT (0x%04x) = %u", HSTXVREGCNT, hstxvregcnt);

		/* HSTXVREGEN */
	hstxvregen = (((1 << num_data_lanes) - 1) << 1) | (1 << 0);
	if (!i2c_write_reg32(tc358748_i2c_client, HSTXVREGEN, hstxvregen))
	{
		pr_err(TAG "Can't write HSTXVREGEN");
		return false;
	}
	pr_info(TAG "HSTXVREGEN (0x%04x) = 0x%08x", HSTXVREGEN, hstxvregen);

		/* TXOPTIONCNTRL */
	if (!i2c_write_reg32(tc358748_i2c_client, TXOPTIONCNTRL, 1))
	{
		pr_err(TAG "Can't write TXOPTIONCNTRL");
		return false;
	}
	pr_info(TAG "TXOPTIONCNTRL (0x%04x) = 1", TXOPTIONCNTRL);

		/* STARTCNTRL */
	if (!i2c_write_reg32(tc358748_i2c_client, STARTCNTRL, 1))
	{
		pr_err(TAG "Can't write STARTCNTRL");
		return false;
	}
	pr_info(TAG "STARTCNTRL (0x%04x) = 1", STARTCNTRL);

		/* CSI_START */
	if (!i2c_write_reg32(tc358748_i2c_client, CSI_START, 1))
	{
		pr_err(TAG "Can't write CSI_START");
		return false;
	}
	pr_info(TAG "CSI_START (0x%04x) = 1", CSI_START);

		/* CSI_CONFW */
	csi_confw = CSI_SET_REGISTER | CSI_CONTROL_REG |
				((num_data_lanes - 1) << 1) |
				(1 << 7) |   /* High-speed mode */
				(1 << 15);   /* CSI mode */
	if (!i2c_write_reg32(tc358748_i2c_client, CSI_CONFW, csi_confw))
	{
		pr_err(TAG "Can't write CSI_CONFW");
		return false;
	}
	pr_info(TAG "CSI_CONFW (0x%04x) = 0x%08x", CSI_CONFW, csi_confw);


		/* Setup the debug output */
		/* DBG_LCNT */
	// dbg_cnt = height - 1;
	// if (!i2c_write_reg16(tc358748_i2c_client, DBG_LCNT, dbg_cnt))
	// {
	// 	pr_err(TAG "Can't write DBG_LCNT");
	// 	return false;
	// }
	// pr_info(TAG "DBG_LCNT (0x%04x) = %d", DBG_LCNT, dbg_cnt);

	// 	/* DBG_WIDTH */
	// dbg_width = total_width * bpp / 8;
	// if (!i2c_write_reg16(tc358748_i2c_client, DBG_WIDTH, (u16)dbg_width))
	// {
	// 	pr_err(TAG "Can't write DBG_WIDTH");
	// 	return false;
	// }
	// pr_info(TAG "DBG_WIDTH (0x%04x) = %d", DBG_WIDTH, dbg_width);

	// 	/* DBG_VBLANK */
	// dbg_vblank = v_sync - 1;
	// if (!i2c_write_reg16(tc358748_i2c_client, DBG_VBLANK, dbg_vblank))
	// {
	// 	pr_err(TAG "Can't write DBG_VBLANK");
	// 	return false;
	// }
	// pr_info(TAG "DBG_VBLANK (0x%04x) = %d", DBG_VBLANK, dbg_vblank);




	// if (!i2c_write_reg16(tc358748_i2c_client, 4, 0x1234))
	// {
	// 	pr_err(TAG "Can't write xxx");
	// }
	// if (!i2c_read_reg16(tc358748_i2c_client, 4, &chip_id))
	// {
	// 	pr_err(TAG "Can't read xxx");
	// 	return false;
	// }

	// pr_info(TAG "xxx = 0x%04x", chip_id);


	// if (!i2c_read_reg32(tc358748_i2c_client, 0x0210, &ui))
	// {
	// 	pr_err(TAG "Can't read xxx");
	// 	return false;
	// }

// pr_info(TAG "xxx = 0x%08x", ui);


	// int err;
	// ctl_regmap = devm_regmap_init_i2c(tc358748_i2c_client, &ctl_regmap_config);
	// if (IS_ERR(ctl_regmap)) {
	// 	dev_err(&tc358748_i2c_client->dev,
	// 		"regmap ctl init failed: %ld\n",
	// 		PTR_ERR(ctl_regmap));
	// 	err = PTR_ERR(ctl_regmap);
	// }

	// tx_regmap = devm_regmap_init_i2c(tc358748_i2c_client, &tx_regmap_config);
	// if (IS_ERR(tx_regmap)) {
	// 	dev_err(&tc358748_i2c_client->dev,
	// 		"regmap csi init failed: %ld\n",
	// 		PTR_ERR(tx_regmap));
	// 	err = PTR_ERR(tx_regmap);
	// }

	// err = regmap_read(tx_regmap, LINEINITCNT, &ui);
	// // err = regmap_write(tx_regmap, LINEINITCNT, ui);

	// pr_info(TAG "xxx = 0x%08x", ui);

	return true;
}



static const struct of_device_id i2c_of_match[] = {
	{
		.compatible = "pco,tc358748_driver"
	},
	{}
};
MODULE_DEVICE_TABLE(of, i2c_of_match);

static struct i2c_device_id i2c_table[] = {
  { "tc358748_driver", 0 },
  {}
};
MODULE_DEVICE_TABLE(i2c, i2c_table);

static int i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	tc358748_i2c_client = client;

	pr_info(TAG "probe driver I2C Toshiba TC358748 - 0x%02x address\n", client->addr);

	tc358748_setup();

	return 0;
}

static int i2c_remove(struct i2c_client *client)
{
	pr_info(TAG "remove driver I2C Toshiba TC358748 - 0x%02x address\n", client->addr);

	tc358748_i2c_client = NULL;

	return 0;
}

static struct i2c_driver tc358748_driver = {
	.driver = {
		.name = "tc358748_driver",
		.of_match_table = i2c_of_match,
	},
  .id_table = i2c_table,
	.probe = i2c_probe,
	.remove = i2c_remove,
};

module_i2c_driver(tc358748_driver);

MODULE_AUTHOR("PCO, p2119, jarsulk, 2024-09");
MODULE_DESCRIPTION("i.MX8MP SOD5 Toshiba TC358748 parallel to CSI-2 driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("v0.900");

/*

nVidia driver TC358748
https://github.com/avionic-design/linux-l4t/blob/meerkat/l4t-r21-5/drivers/media/i2c/tc358748.c

*/