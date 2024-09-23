// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/math.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
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
#define CLW_CNTRL       0x0140
#define D0W_CNTRL       0x0144
#define D1W_CNTRL       0x0148
#define D2W_CNTRL       0x014c
#define D3W_CNTRL       0x0150
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
#define CSI_RESET       0x0504
#define CSI_START       0x0518

#define DBG_LCNT        0x00E0
#define DBG_WIDTH       0x00E2
#define DBG_VBLANK      0x00E4
#define DBG_DATA        0x00E8

/* Values used in the CSI_CONFW register */
#define CSI_SET_REGISTER	(5 << 29)
#define CSI_CLR_REGISTER	(6 << 29)
#define CSI_CONTROL_REG		(3 << 24)

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
bool tc358748_setup(struct i2c_client *client)
{
	u16 chip_id;
	u16 confctl;
	u16 fifoctl;
	u16 datafmt;
	u16 wordcnt;

	const u16 width = 640;
	// const u16 height = 480;
	// const u16 total_width = 800;
	// // const u16 total_height = 525;
	// // const u16 h_front_porch = 16;
	// // const u16 h_sync = 96;
	// // const u16 h_back_porch = 48;
	// // const u16 v_front_porch = 10;
	// const u16 v_sync = 2;
	// // const u16 v_back_porch = 33;

	const u8 bpp = 24;
	const u8 num_data_lanes = 4;
	const u32 pixelclock = 12676060;                      // 800 * 525 * 30,181095238 = 12'676'060
	const u32 csi_bus = 38028180;                         // 38'028'180 z DDR  $$
	// const u32 csi_bus = 76056360;                         // 76'056'360 bez DDR
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
	// u16 dbg_cnt;
	// u32 dbg_width;
	// u16 dbg_vblank;

	tc358748_i2c_client = client;

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

	usleep_range(50 * 1000, 50 * 1000);
	pr_info(TAG "Wait 50ms");

		/* setup PLL */
	if (!tc358748_set_pll())
	{
		return false;
	}

		/* CONFCTL */
	confctl = num_data_lanes - 1;
	confctl |=
			(1 << 2) |  /* I2C slave index increment */
			// (1 << 3) |  /* Parallel clock polarity inverted - $$ nVidia driver */
			(1 << 4) |  /* H Sync active low */
			// (1 << 5) |  /* V Sync active low */
			(1 << 6) |  /* Parallel port enable - $$ nVidia driver */
			// (0 << 8);   /* Parallel data format - mode 0 */
			(3 << 8);   /* Parallel data format - reserved - $$ nVidia driver */
	if (!i2c_write_reg16(tc358748_i2c_client, CONFCTL, confctl))
	{
		pr_err(TAG "Can't write CONFCTL");
		return false;
	}
	pr_info(TAG "CONFCTL (0x%04x) = 0x%04x", CONFCTL, confctl);



// $$ działa za każdym razem w 640 RGB888, ale nie działa reset software'owy - resetować zasilaniem

// i2c_write_reg16(tc358748_i2c_client, DATAFMT, 0x60);
i2c_write_reg16(tc358748_i2c_client, DATAFMT, 0x30);   // RGB888
i2c_write_reg16(tc358748_i2c_client, CONFCTL, confctl);
i2c_write_reg16(tc358748_i2c_client, FIFOCTL, 0x20);
// i2c_write_reg16(tc358748_i2c_client, WORDCNT, 0xf00);
i2c_write_reg16(tc358748_i2c_client, WORDCNT, 640 * 3); // 640
i2c_write_reg32(tc358748_i2c_client, CLW_CNTRL, 0x140);
i2c_write_reg32(tc358748_i2c_client, D0W_CNTRL, 0x144);
i2c_write_reg32(tc358748_i2c_client, D1W_CNTRL, 0x148);
i2c_write_reg32(tc358748_i2c_client, D2W_CNTRL, 0x14c);
i2c_write_reg32(tc358748_i2c_client, D3W_CNTRL, 0x150);
i2c_write_reg32(tc358748_i2c_client, LINEINITCNT, 0x15ba);
i2c_write_reg32(tc358748_i2c_client, LPTXTIMECNT, 0x2);
i2c_write_reg32(tc358748_i2c_client, TCLK_HEADERCNT, 0xa03);

// i2c_write_reg32(tc358748_i2c_client, TCLK_TRAILCNT, 0xffffffff);
i2c_write_reg32(tc358748_i2c_client, TCLK_TRAILCNT, 1);
// i2c_write_reg32(tc358748_i2c_client, THS_HEADERCNT, 0xffffee03);
// i2c_write_reg32(tc358748_i2c_client, THS_HEADERCNT, 0xffffffff - 0xffffee03);
i2c_write_reg32(tc358748_i2c_client, THS_HEADERCNT, 0x0101);

i2c_write_reg32(tc358748_i2c_client, TWAKEUP, 0x49e0);
i2c_write_reg32(tc358748_i2c_client, TCLK_POSTCNT, 0x7);
i2c_write_reg32(tc358748_i2c_client, THS_TRAILCNT, 0x1);
i2c_write_reg32(tc358748_i2c_client, HSTXVREGEN, 0x1f);
i2c_write_reg32(tc358748_i2c_client, STARTCNTRL, 0x1);
i2c_write_reg32(tc358748_i2c_client, CSI_START, 0x1);
i2c_write_reg32(tc358748_i2c_client, CSI_CONFW, 2734719110);
return true;
}
