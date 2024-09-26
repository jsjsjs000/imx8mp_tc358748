// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/math.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>




#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/of_graph.h>
#include <linux/videodev2.h>
#include <linux/workqueue.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/hdmi.h>
#include <media/cec.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include "tc358743.h"




#define EDID_NUM_BLOCKS_MAX 8
#define EDID_BLOCK_SIZE 128
#define POLL_INTERVAL_CEC_MS	10
#define POLL_INTERVAL_MS	1000

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-3)");

static const struct v4l2_dv_timings_cap tc358743_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	/* keep this initialization for compatibility with GCC < 4.4.6 */
	.reserved = { 0 },
	/* Pixel clock from REF_01 p. 20. Min/max height/width are unknown */
	V4L2_INIT_BT_TIMINGS(640, 1920, 350, 1200, 13000000, 165000000,
			V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
			V4L2_DV_BT_CAP_PROGRESSIVE |
			V4L2_DV_BT_CAP_REDUCED_BLANKING |
			V4L2_DV_BT_CAP_CUSTOM)
};


struct tc358743_state {
	struct tc358743_platform_data pdata;
	struct v4l2_fwnode_bus_mipi_csi2 bus;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler hdl;
	struct i2c_client *i2c_client;
	/* CONFCTL is modified in ops and tc358743_hdmi_sys_int_handler */
	struct mutex confctl_mutex;

	/* controls */
	struct v4l2_ctrl *detect_tx_5v_ctrl;
	struct v4l2_ctrl *audio_sampling_rate_ctrl;
	struct v4l2_ctrl *audio_present_ctrl;

	struct delayed_work delayed_work_enable_hotplug;

	struct timer_list timer;
	struct work_struct work_i2c_poll;

	/* edid  */
	u8 edid_blocks_written;

	struct v4l2_dv_timings timings;
	u32 mbus_fmt_code;
	u8 csi_lanes_in_use;

	struct gpio_desc *reset_gpio;

	struct cec_adapter *cec_adap;
};







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
// static bool tc358748_setup(void)
// {
// 	u16 chip_id;
// 	u16 confctl;
// 	u16 fifoctl;
// 	u16 datafmt;
// 	u16 wordcnt;

// 	const u16 width = 640;
// 	// const u16 height = 480;
// 	// const u16 total_width = 800;
// 	// // const u16 total_height = 525;
// 	// // const u16 h_front_porch = 16;
// 	// // const u16 h_sync = 96;
// 	// // const u16 h_back_porch = 48;
// 	// // const u16 v_front_porch = 10;
// 	// const u16 v_sync = 2;
// 	// // const u16 v_back_porch = 33;

// 	const u8 bpp = 24;
// 	const u8 num_data_lanes = 4;
// 	const u32 pixelclock = 12676060;                      // 800 * 525 * 30,181095238 = 12'676'060
// 	const u32 csi_bus = 38028180;                         // 38'028'180 z DDR  $$
// 	// const u32 csi_bus = 76056360;                         // 76'056'360 bez DDR
// 	const u32 csi_rate = bpp * pixelclock;                // 304'225'440 bps
// 	const u32 csi_lane_rate = csi_rate / num_data_lanes;  // 76'056'360 (min 62'500'000, max 1G)

// 	u32 hsbyte_clk;
// 	u32 linecnt;
// 	u32 lptxtime;
// 	u32 t_wakeup;
// 	u32 tclk_prepare;
// 	u32 tclk_zero;
// 	u32 tclk_trail;
// 	u32 tclk_post;
// 	u32 ths_prepare;
// 	u32 ths_zero;
// 	u32 ths_trail;

// 	u32 tclk_headercnt;
// 	u32 ths_headercnt;
// 	u32 hstxvregcnt;
// 	u32 hstxvregen;
// 	u32 csi_confw;
// 	// u16 dbg_cnt;
// 	// u32 dbg_width;
// 	// u16 dbg_vblank;

// 	pr_info(TAG "  bpp = %d", bpp);
// 	pr_info(TAG "  num_data_lanes = %d", num_data_lanes);
// 	pr_info(TAG "  pixelclock = %u", pixelclock);
// 	pr_info(TAG "  csi_bus = %u", csi_bus);
// 	pr_info(TAG "  csi_rate = %u", csi_rate);
// 	pr_info(TAG "  csi_lane_rate = %u", csi_lane_rate);

// 	if (!i2c_read_reg16(tc358748_i2c_client, CHIPID, &chip_id))
// 	{
// 		pr_err(TAG "Can't read ChipId");
// 		return false;
// 	}

// 	if (chip_id != 0x4401)
// 	{
// 		pr_err(TAG "Chip not found - ChipId 0x04%x is not 0x4401", chip_id);
// 		return false;
// 	}
// 	pr_info(TAG "ChipId (0x%04x) = 0x%04x - ok", CHIPID, chip_id);

// 		/* Reset */
// 	if (!i2c_write_reg16(tc358748_i2c_client, SYSCTL, 1))
// 	{
// 		pr_err(TAG "Can't write SYSCTL");
// 		return false;
// 	}
// 	pr_info(TAG "SYSCTL (0x%04x) = 1 - Reset", SYSCTL);

// 	usleep_range(50 * 1000, 50 * 1000);
// 	pr_info(TAG "Wait 50ms");

// 		/* End of reset */
// 	if (!i2c_write_reg16(tc358748_i2c_client, SYSCTL, 0))
// 	{
// 		pr_err(TAG "Can't write SYSCTL");
// 		return false;
// 	}
// 	pr_info(TAG "SYSCTL (0x%04x) = 0 - End of reset", SYSCTL);

// 	usleep_range(50 * 1000, 50 * 1000);
// 	pr_info(TAG "Wait 50ms");

// 		/* setup PLL */
// 	if (!tc358748_set_pll())
// 	{
// 		return false;
// 	}

// 		/* CONFCTL */
// 	confctl = num_data_lanes - 1;
// 	confctl |=
// 			(1 << 2) |  /* I2C slave index increment */
// 			// (1 << 3) |  /* Parallel clock polarity inverted - $$ nVidia driver */
// 			(1 << 4) |  /* H Sync active low */
// 			// (1 << 5) |  /* V Sync active low */
// 			// (1 << 6) |  /* Parallel port enable - $$ nVidia driver */
// 			// (0 << 8);   /* Parallel data format - mode 0 */
// 			(3 << 8);   /* Parallel data format - reserved - $$ nVidia driver */
// 	if (!i2c_write_reg16(tc358748_i2c_client, CONFCTL, confctl))
// 	{
// 		pr_err(TAG "Can't write CONFCTL");
// 		return false;
// 	}
// 	pr_info(TAG "CONFCTL (0x%04x) = 0x%04x", CONFCTL, confctl);



// // $$ działa za każdym razem w 640 RGB888, ale nie działa reset software'owy - resetować zasilaniem

// // i2c_write_reg16(tc358748_i2c_client, DATAFMT, 0x60);
// i2c_write_reg16(tc358748_i2c_client, DATAFMT, 0x30);   // RGB888
// i2c_write_reg16(tc358748_i2c_client, CONFCTL, confctl);
// i2c_write_reg16(tc358748_i2c_client, FIFOCTL, 0x20);
// // i2c_write_reg16(tc358748_i2c_client, WORDCNT, 0xf00);
// i2c_write_reg16(tc358748_i2c_client, WORDCNT, 640 * 3); // 640

// confctl |= (1 << 6);                                   /* Parallel port enable */
// i2c_write_reg16(tc358748_i2c_client, CONFCTL, confctl);

// i2c_write_reg32(tc358748_i2c_client, CSI_START, 0x1);
// usleep_range(10 * 1000, 10 * 1000);

// i2c_write_reg32(tc358748_i2c_client, CLW_CNTRL, 0x140);
// i2c_write_reg32(tc358748_i2c_client, D0W_CNTRL, 0x144);
// i2c_write_reg32(tc358748_i2c_client, D1W_CNTRL, 0x148);
// i2c_write_reg32(tc358748_i2c_client, D2W_CNTRL, 0x14c);
// i2c_write_reg32(tc358748_i2c_client, D3W_CNTRL, 0x150);
// i2c_write_reg32(tc358748_i2c_client, LINEINITCNT, 0x15ba);
// i2c_write_reg32(tc358748_i2c_client, LPTXTIMECNT, 0x2);
// i2c_write_reg32(tc358748_i2c_client, TCLK_HEADERCNT, 0xa03);

// // i2c_write_reg32(tc358748_i2c_client, TCLK_TRAILCNT, 0xffffffff);
// i2c_write_reg32(tc358748_i2c_client, TCLK_TRAILCNT, 1);
// // i2c_write_reg32(tc358748_i2c_client, THS_HEADERCNT, 0xffffee03);
// // i2c_write_reg32(tc358748_i2c_client, THS_HEADERCNT, 0xffffffff - 0xffffee03);
// i2c_write_reg32(tc358748_i2c_client, THS_HEADERCNT, 0x0101);

// i2c_write_reg32(tc358748_i2c_client, TWAKEUP, 0x49e0);
// i2c_write_reg32(tc358748_i2c_client, TCLK_POSTCNT, 0x7);
// i2c_write_reg32(tc358748_i2c_client, THS_TRAILCNT, 0x1);
// i2c_write_reg32(tc358748_i2c_client, HSTXVREGEN, 0x1f);
// i2c_write_reg32(tc358748_i2c_client, STARTCNTRL, 0x1);
// i2c_write_reg32(tc358748_i2c_client, CSI_CONFW, 2734719110);
// return true;






// 		/* FIFOCTL - FiFo level */
// 	fifoctl = 1; // 12 RGB888 ;//16;  // $$
// 	if (!i2c_write_reg16(tc358748_i2c_client, FIFOCTL, fifoctl))
// 	{
// 		pr_err(TAG "Can't write FIFOCTL");
// 		return false;
// 	}
// 	pr_info(TAG "FIFOCTL (0x%04x) = %d - FiFo Level", FIFOCTL, fifoctl);

// 		/* DATAFMT - Data Format */
// 	datafmt = (3 << 4);  /* 3 - RGB888 */
// 	if (!i2c_write_reg16(tc358748_i2c_client, DATAFMT, datafmt))
// 	{
// 		pr_err(TAG "Can't write DATAFMT");
// 		return false;
// 	}
// 	pr_info(TAG "DATAFMT (0x%04x) = 0x%04x - Data Format", DATAFMT, datafmt);

// 		/* WORDCNT */
// 	wordcnt = width * bpp / 8;
// 	if (!i2c_write_reg16(tc358748_i2c_client, WORDCNT, wordcnt))
// 	{
// 		pr_err(TAG "Can't write WORDCNT");
// 		return false;
// 	}
// 	pr_info(TAG "WORDCNT (0x%04x) = %d - Word count", WORDCNT, wordcnt);




// i2c_write_reg32(tc358748_i2c_client, LINEINITCNT, 0x15ba - 0x000);
// i2c_write_reg32(tc358748_i2c_client, LPTXTIMECNT, 0x2 + 2);
// i2c_write_reg32(tc358748_i2c_client, TCLK_HEADERCNT, 0xa03 - 0x100);

// // i2c_write_reg32(tc358748_i2c_client, TCLK_TRAILCNT, 0xffffffff);
// i2c_write_reg32(tc358748_i2c_client, TCLK_TRAILCNT, 1 + 6);

// // i2c_write_reg32(tc358748_i2c_client, THS_HEADERCNT, 0xffffee03);
// i2c_write_reg32(tc358748_i2c_client, THS_HEADERCNT, 0x0303 - 0x202);

// i2c_write_reg32(tc358748_i2c_client, TWAKEUP, 0x49e0 * 1);
// i2c_write_reg32(tc358748_i2c_client, TCLK_POSTCNT, 0x7 -3);
// i2c_write_reg32(tc358748_i2c_client, THS_TRAILCNT, 0x1 + 2);

// i2c_write_reg32(tc358748_i2c_client, HSTXVREGEN, 0x1f);
// i2c_write_reg32(tc358748_i2c_client, STARTCNTRL, 0x1);
// i2c_write_reg32(tc358748_i2c_client, CSI_START, 0x1);
// i2c_write_reg32(tc358748_i2c_client, CSI_CONFW, 2734719110);
// return true;



// // my settings:
// // PLLCTL0 (0x0016) = 0x117f - Setup PLL divider
// // PLLCTL1 (0x0018) = 0x0e03 - Start PLL
// // CLKCTL (0x0020) = 0x002a - Setup PLL divider
// // PLLCTL1 (0x0018) = 0x0e13 - Turn on clocks
// // CONFCTL (0x0004) = 0x037f
// // FIFOCTL (0x0006) = 1 - FiFo Level
// // DATAFMT (0x0008) = 0x0030 - Data Format
// // WORDCNT (0x0022) = 1920 - Word count


// 		/* Compute the D-PHY settings */
// 	hsbyte_clk = csi_lane_rate / 8;

// 		/* LINEINITCOUNT >= 100us */
// 	linecnt = clk_count(hsbyte_clk / 2, 100000);

// 		/* LPTX clk must be less than 20MHz -> LPTXTIMECNT >= 50 ns */
// 	lptxtime = clk_count(hsbyte_clk, 50);

// 		/* TWAKEUP >= 1ms (in LPTX clock count) */
// 	t_wakeup = clk_count(hsbyte_clk / lptxtime, 1000000);

// 		/* 38ns <= TCLK_PREPARE <= 95ns */
// 	tclk_prepare = clk_count(hsbyte_clk, 38);
// 	if (tclk_prepare > clk_count(hsbyte_clk, 95))
// 		pr_warn(TAG "TCLK_PREPARE is too long (%u ns)\n",
// 			clk_ns(hsbyte_clk, tclk_prepare));
// 	// TODO: Check that TCLK_PREPARE <= 95ns

// 		/* TCLK_ZERO + TCLK_PREPARE >= 300ns */
// 	tclk_zero = clk_count(hsbyte_clk, 300) - tclk_prepare;

// 		/* TCLK_TRAIL >= 60ns */
// 	tclk_trail = clk_count(hsbyte_clk, 60);

// 		/* TCLK_POST >= 60ns + 52*UI */
// 	tclk_post = clk_count(hsbyte_clk, 60 + clk_ns(csi_lane_rate, 52));

// 		/* 40ns + 4*UI <= THS_PREPARE <= 85ns + 6*UI */
// 	ths_prepare = clk_count(hsbyte_clk, 40 + clk_ns(csi_lane_rate, 4));
// 	if (ths_prepare > 85 + clk_ns(csi_lane_rate, 6))
// 		pr_warn(TAG "THS_PREPARE is too long (%u ns)\n",
// 			clk_ns(hsbyte_clk, ths_prepare));

// 		/* THS_ZERO + THS_PREPARE >= 145ns + 10*UI */
// 	ths_zero = clk_count(hsbyte_clk, 145 +
// 			clk_ns(csi_lane_rate, 10)) - ths_prepare;

// 		/* 105ns + 12*UI > THS_TRAIL >= max(8*UI, 60ns + 4*UI) */
// 	ths_trail = clk_count(hsbyte_clk,
// 			max(clk_ns(csi_lane_rate, 8),
// 				60 + clk_ns(csi_lane_rate, 4)));

// 	pr_info(TAG "  hsbyte_clk = %u", hsbyte_clk);
// 	pr_info(TAG "  linecnt = %u", linecnt);
// 	pr_info(TAG "  lptxtime = %u", lptxtime);
// 	pr_info(TAG "  t_wakeup = %u", t_wakeup);
// 	pr_info(TAG "  tclk_prepare = %u", tclk_prepare);
// 	pr_info(TAG "  tclk_zero = %u", tclk_zero);
// 	pr_info(TAG "  tclk_trail = %u", tclk_trail);
// 	pr_info(TAG "  tclk_post = %u", tclk_post);
// 	pr_info(TAG "  ths_prepare = %u", ths_prepare);
// 	pr_info(TAG "  ths_zero = %u", ths_zero);
// 	pr_info(TAG "  ths_trail = %u", ths_trail);


// 		/* Setup D-PHY */
// 		/* LINEINITCNT */
// 	if (!i2c_write_reg32(tc358748_i2c_client, LINEINITCNT, linecnt))
// 	{
// 		pr_err(TAG "Can't write LINEINITCNT");
// 		return false;
// 	}
// 	pr_info(TAG "LINEINITCNT (0x%04x) = %u", LINEINITCNT, linecnt);

// 		/* LPTXTIMECNT */
// 	if (!i2c_write_reg32(tc358748_i2c_client, LPTXTIMECNT, lptxtime))
// 	{
// 		pr_err(TAG "Can't write LPTXTIMECNT");
// 		return false;
// 	}
// 	pr_info(TAG "LPTXTIMECNT (0x%04x) = %u", LPTXTIMECNT, lptxtime);

// 		/* TCLK_HEADERCNT */
// 	tclk_headercnt = tclk_prepare | (tclk_zero << 8);
// 	if (!i2c_write_reg32(tc358748_i2c_client, TCLK_HEADERCNT, tclk_headercnt))
// 	{
// 		pr_err(TAG "Can't write TCLK_HEADERCNT");
// 		return false;
// 	}
// 	pr_info(TAG "TCLK_HEADERCNT (0x%04x) = 0x%08x", TCLK_HEADERCNT, tclk_headercnt);

// 		/* TCLK_TRAILCNT */
// 	if (!i2c_write_reg32(tc358748_i2c_client, TCLK_TRAILCNT, tclk_trail))
// 	{
// 		pr_err(TAG "Can't write TCLK_TRAILCNT");
// 		return false;
// 	}
// 	pr_info(TAG "TCLK_TRAILCNT (0x%04x) = %u", TCLK_TRAILCNT, tclk_trail);

// 		/* THS_HEADERCNT */
// 	ths_headercnt = ths_prepare | (ths_zero << 8);
// 	if (!i2c_write_reg32(tc358748_i2c_client, THS_HEADERCNT, ths_headercnt))
// 	{
// 		pr_err(TAG "Can't write THS_HEADERCNT");
// 		return false;
// 	}
// 	pr_info(TAG "THS_HEADERCNT (0x%04x) = 0x%08x", THS_HEADERCNT, ths_headercnt);

// 		/* TWAKEUP */
// // t_wakeup=1; // $$
// 	if (!i2c_write_reg32(tc358748_i2c_client, TWAKEUP, t_wakeup))
// 	{
// 		pr_err(TAG "Can't write TWAKEUP");
// 		return false;
// 	}
// 	pr_info(TAG "TWAKEUP (0x%04x) = %u", TWAKEUP, t_wakeup);

// 		/* TCLK_POSTCNT */
// 	if (!i2c_write_reg32(tc358748_i2c_client, TCLK_POSTCNT, tclk_post))
// 	{
// 		pr_err(TAG "Can't write TCLK_POSTCNT");
// 		return false;
// 	}
// 	pr_info(TAG "TCLK_POSTCNT (0x%04x) = %u", TCLK_POSTCNT, tclk_post);

// 		/* THS_TRAILCNT */
// 	if (!i2c_write_reg32(tc358748_i2c_client, THS_TRAILCNT, ths_trail))
// 	{
// 		pr_err(TAG "Can't write THS_TRAILCNT");
// 		return false;
// 	}
// 	pr_info(TAG "THS_TRAILCNT (0x%04x) = %u", THS_TRAILCNT, ths_trail);

// 		/* HSTXVREGCNT */
// 	hstxvregcnt = 5;
// 	if (!i2c_write_reg32(tc358748_i2c_client, HSTXVREGCNT, hstxvregcnt))
// 	{
// 		pr_err(TAG "Can't write HSTXVREGCNT");
// 		return false;
// 	}
// 	pr_info(TAG "HSTXVREGCNT (0x%04x) = %u", HSTXVREGCNT, hstxvregcnt);

// 		/* HSTXVREGEN */
// 	hstxvregen = (((1 << num_data_lanes) - 1) << 1) | (1 << 0);
// 	if (!i2c_write_reg32(tc358748_i2c_client, HSTXVREGEN, hstxvregen))
// 	{
// 		pr_err(TAG "Can't write HSTXVREGEN");
// 		return false;
// 	}
// 	pr_info(TAG "HSTXVREGEN (0x%04x) = 0x%08x", HSTXVREGEN, hstxvregen);

// 		/* TXOPTIONCNTRL */
// 	if (!i2c_write_reg32(tc358748_i2c_client, TXOPTIONCNTRL, 1))
// 	{
// 		pr_err(TAG "Can't write TXOPTIONCNTRL");
// 		return false;
// 	}
// 	pr_info(TAG "TXOPTIONCNTRL (0x%04x) = 1", TXOPTIONCNTRL);

// 		/* STARTCNTRL */
// 	if (!i2c_write_reg32(tc358748_i2c_client, STARTCNTRL, 1))
// 	{
// 		pr_err(TAG "Can't write STARTCNTRL");
// 		return false;
// 	}
// 	pr_info(TAG "STARTCNTRL (0x%04x) = 1", STARTCNTRL);

// 		/* CSI_START */
// 	if (!i2c_write_reg32(tc358748_i2c_client, CSI_START, 1))
// 	{
// 		pr_err(TAG "Can't write CSI_START");
// 		return false;
// 	}
// 	pr_info(TAG "CSI_START (0x%04x) = 1", CSI_START);

// 		/* CSI_CONFW */
// 	csi_confw = CSI_SET_REGISTER | CSI_CONTROL_REG |
// 				((num_data_lanes - 1) << 1) |
// 				(1 << 7) |   /* High-speed mode */
// 				(1 << 15);   /* CSI mode */
// 	if (!i2c_write_reg32(tc358748_i2c_client, CSI_CONFW, csi_confw))
// 	{
// 		pr_err(TAG "Can't write CSI_CONFW");
// 		return false;
// 	}
// 	pr_info(TAG "CSI_CONFW (0x%04x) = 0x%08x", CSI_CONFW, csi_confw);


// 		/* Setup the debug output */
// 		/* DBG_LCNT */
// 	// dbg_cnt = height - 1;
// 	// if (!i2c_write_reg16(tc358748_i2c_client, DBG_LCNT, dbg_cnt))
// 	// {
// 	// 	pr_err(TAG "Can't write DBG_LCNT");
// 	// 	return false;
// 	// }
// 	// pr_info(TAG "DBG_LCNT (0x%04x) = %d", DBG_LCNT, dbg_cnt);

// 	// 	/* DBG_WIDTH */
// 	// dbg_width = total_width * bpp / 8;
// 	// if (!i2c_write_reg16(tc358748_i2c_client, DBG_WIDTH, (u16)dbg_width))
// 	// {
// 	// 	pr_err(TAG "Can't write DBG_WIDTH");
// 	// 	return false;
// 	// }
// 	// pr_info(TAG "DBG_WIDTH (0x%04x) = %d", DBG_WIDTH, dbg_width);

// 	// 	/* DBG_VBLANK */
// 	// dbg_vblank = v_sync - 1;
// 	// if (!i2c_write_reg16(tc358748_i2c_client, DBG_VBLANK, dbg_vblank))
// 	// {
// 	// 	pr_err(TAG "Can't write DBG_VBLANK");
// 	// 	return false;
// 	// }
// 	// pr_info(TAG "DBG_VBLANK (0x%04x) = %d", DBG_VBLANK, dbg_vblank);




// 	// if (!i2c_write_reg16(tc358748_i2c_client, 4, 0x1234))
// 	// {
// 	// 	pr_err(TAG "Can't write xxx");
// 	// }
// 	// if (!i2c_read_reg16(tc358748_i2c_client, 4, &chip_id))
// 	// {
// 	// 	pr_err(TAG "Can't read xxx");
// 	// 	return false;
// 	// }

// 	// pr_info(TAG "xxx = 0x%04x", chip_id);


// 	// if (!i2c_read_reg32(tc358748_i2c_client, 0x0210, &ui))
// 	// {
// 	// 	pr_err(TAG "Can't read xxx");
// 	// 	return false;
// 	// }

// // pr_info(TAG "xxx = 0x%08x", ui);


// 	// int err;
// 	// ctl_regmap = devm_regmap_init_i2c(tc358748_i2c_client, &ctl_regmap_config);
// 	// if (IS_ERR(ctl_regmap)) {
// 	// 	dev_err(&tc358748_i2c_client->dev,
// 	// 		"regmap ctl init failed: %ld\n",
// 	// 		PTR_ERR(ctl_regmap));
// 	// 	err = PTR_ERR(ctl_regmap);
// 	// }

// 	// tx_regmap = devm_regmap_init_i2c(tc358748_i2c_client, &tx_regmap_config);
// 	// if (IS_ERR(tx_regmap)) {
// 	// 	dev_err(&tc358748_i2c_client->dev,
// 	// 		"regmap csi init failed: %ld\n",
// 	// 		PTR_ERR(tx_regmap));
// 	// 	err = PTR_ERR(tx_regmap);
// 	// }

// 	// err = regmap_read(tx_regmap, LINEINITCNT, &ui);
// 	// // err = regmap_write(tx_regmap, LINEINITCNT, ui);

// 	// pr_info(TAG "xxx = 0x%08x", ui);

// 	return true;
// }



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



static inline struct tc358743_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct tc358743_state, sd);
}

static int tc358743_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	*status = 0;
	// *status |= no_signal(sd) ? V4L2_IN_ST_NO_SIGNAL : 0;
	// *status |= no_sync(sd) ? V4L2_IN_ST_NO_SYNC : 0;

	v4l2_dbg(1, debug, sd, "%s: status = 0x%x\n", __func__, *status);

	return 0;
}

static int tc358743_s_dv_timings(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings *timings)
{
	struct tc358743_state *state = to_state(sd);
pr_info(TAG "  $$ k");

	if (!timings)
		return -EINVAL;
pr_info(TAG "  $$ k2");

	// if (debug)
		v4l2_print_dv_timings(sd->name, "tc358743_s_dv_timings: ",
				timings, false);

	if (v4l2_match_dv_timings(&state->timings, timings, 0, false)) {
		v4l2_dbg(1, debug, sd, "%s: no change\n", __func__);
		return 0;
	}

pr_info(TAG "  $$ k3");

	if (!v4l2_valid_dv_timings(timings,
				&tc358743_timings_cap, NULL, NULL)) {
		v4l2_dbg(1, debug, sd, "%s: timings out of range\n", __func__);
		return -ERANGE;
	}

	state->timings = *timings;

pr_info(TAG "  $$ k4");

	// enable_stream(sd, false);
	// tc358743_set_pll(sd);
	// tc358743_set_csi(sd);

	return 0;
}

static int tc358743_g_dv_timings(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings *timings)
{
	struct tc358743_state *state = to_state(sd);

pr_info(TAG "  $$ b");
	*timings = state->timings;

	return 0;
}

static int tc358743_enum_dv_timings(struct v4l2_subdev *sd,
				    struct v4l2_enum_dv_timings *timings)
{
pr_info(TAG "  $$ c");
	if (timings->pad != 0)
		return -EINVAL;

	return v4l2_enum_dv_timings_cap(timings,
			&tc358743_timings_cap, NULL, NULL);
}

static int tc358743_query_dv_timings(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	// int ret;

	// ret = tc358743_get_detected_timings(sd, timings);
	// if (ret)
	// 	return ret;

	// if (debug)
	// 	v4l2_print_dv_timings(sd->name, "tc358743_query_dv_timings: ",
	// 			timings, false);

pr_info(TAG "  $$ d");

	if (!v4l2_valid_dv_timings(timings,
				&tc358743_timings_cap, NULL, NULL)) {
		v4l2_dbg(1, debug, sd, "%s: timings out of range\n", __func__);
		return -ERANGE;
	}

	return 0;
}

static int tc358743_dv_timings_cap(struct v4l2_subdev *sd,
		struct v4l2_dv_timings_cap *cap)
{
pr_info(TAG "  $$ e");
	if (cap->pad != 0)
		return -EINVAL;

	*cap = tc358743_timings_cap;

	return 0;
}

static int tc358743_get_mbus_config(struct v4l2_subdev *sd,
				    unsigned int pad,
				    struct v4l2_mbus_config *cfg)
{
	struct tc358743_state *state = to_state(sd);

pr_info(TAG "  $$ f");
	cfg->type = V4L2_MBUS_CSI2_DPHY;

	/* Support for non-continuous CSI-2 clock is missing in the driver */
	cfg->flags = V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	switch (state->csi_lanes_in_use) {
	case 1:
		cfg->flags |= V4L2_MBUS_CSI2_1_LANE;
		break;
	case 2:
		cfg->flags |= V4L2_MBUS_CSI2_2_LANE;
		break;
	case 3:
		cfg->flags |= V4L2_MBUS_CSI2_3_LANE;
		break;
	case 4:
		cfg->flags |= V4L2_MBUS_CSI2_4_LANE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tc358743_s_stream(struct v4l2_subdev *sd, int enable)
{
pr_info(TAG "  $$ g");
	// enable_stream(sd, enable);
	if (!enable) {
		/* Put all lanes in LP-11 state (STOPSTATE) */
		// tc358743_set_csi(sd);
	}

	return 0;
}

/* --------------- PAD OPS --------------- */

static int tc358743_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_mbus_code_enum *code)
{
pr_info(TAG "  $$ h");
	switch (code->index) {
	case 0:
		code->code = MEDIA_BUS_FMT_RGB888_1X24;
		break;
	case 1:
		code->code = MEDIA_BUS_FMT_UYVY8_1X16;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int tc358743_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_format *format)
{
	struct tc358743_state *state = to_state(sd);
	// u8 vi_rep = i2c_rd8(sd, VI_REP);

pr_info(TAG "  $$ i");
	if (format->pad != 0)
		return -EINVAL;

	format->format.code = state->mbus_fmt_code;
	format->format.width = state->timings.bt.width;
	format->format.height = state->timings.bt.height;
	format->format.field = V4L2_FIELD_NONE;

	// switch (vi_rep & MASK_VOUT_COLOR_SEL) {
	// case MASK_VOUT_COLOR_RGB_FULL:
	// case MASK_VOUT_COLOR_RGB_LIMITED:
	// 	format->format.colorspace = V4L2_COLORSPACE_SRGB;
	// 	break;
	// case MASK_VOUT_COLOR_601_YCBCR_LIMITED:
	// case MASK_VOUT_COLOR_601_YCBCR_FULL:
	// 	format->format.colorspace = V4L2_COLORSPACE_SMPTE170M;
	// 	break;
	// case MASK_VOUT_COLOR_709_YCBCR_FULL:
	// case MASK_VOUT_COLOR_709_YCBCR_LIMITED:
	// 	format->format.colorspace = V4L2_COLORSPACE_REC709;
	// 	break;
	// default:
	// 	format->format.colorspace = 0;
	// 	break;
	// }

	return 0;
}

static int tc358743_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_format *format)
{
	struct tc358743_state *state = to_state(sd);

pr_info(TAG "  $$ j");
	u32 code = format->format.code; /* is overwritten by get_fmt */
	int ret = tc358743_get_fmt(sd, sd_state, format);

	format->format.code = code;

	if (ret)
		return ret;

	switch (code) {
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_UYVY8_1X16:
		break;
	default:
		return -EINVAL;
	}

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	state->mbus_fmt_code = format->format.code;

	// enable_stream(sd, false);
	// tc358743_set_pll(sd);
	// tc358743_set_csi(sd);
	// tc358743_set_csi_color_space(sd);

	return 0;
}

static int tc358743_g_edid(struct v4l2_subdev *sd,
		struct v4l2_subdev_edid *edid)
{
	struct tc358743_state *state = to_state(sd);

	pr_info(TAG "  $$ g_edid");

	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->pad != 0)
		return -EINVAL;

	if (edid->start_block == 0 && edid->blocks == 0) {
		edid->blocks = state->edid_blocks_written;
		return 0;
	}

	if (state->edid_blocks_written == 0)
		return -ENODATA;

	if (edid->start_block >= state->edid_blocks_written ||
			edid->blocks == 0)
		return -EINVAL;

	if (edid->start_block + edid->blocks > state->edid_blocks_written)
		edid->blocks = state->edid_blocks_written - edid->start_block;

	// i2c_rd(sd, EDID_RAM + (edid->start_block * EDID_BLOCK_SIZE), edid->edid,
	// 		edid->blocks * EDID_BLOCK_SIZE);

	return 0;
}

static int tc358743_s_edid(struct v4l2_subdev *sd,
				struct v4l2_subdev_edid *edid)
{

	struct tc358743_state *state = to_state(sd);
	// u16 edid_len = edid->blocks * EDID_BLOCK_SIZE;
	u16 pa;
	int err;
	// int i;

	pr_info(TAG "  $$ e_did");

	v4l2_dbg(2, debug, sd, "%s, pad %d, start block %d, blocks %d\n",
		 __func__, edid->pad, edid->start_block, edid->blocks);

	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->pad != 0)
		return -EINVAL;

	if (edid->start_block != 0)
		return -EINVAL;

	if (edid->blocks > EDID_NUM_BLOCKS_MAX) {
		edid->blocks = EDID_NUM_BLOCKS_MAX;
		return -E2BIG;
	}
	pa = cec_get_edid_phys_addr(edid->edid, edid->blocks * 128, NULL);
	err = v4l2_phys_addr_validate(pa, &pa, NULL);
	if (err)
		return err;

	cec_phys_addr_invalidate(state->cec_adap);

	// tc358743_disable_edid(sd);

	// i2c_wr8(sd, EDID_LEN1, edid_len & 0xff);
	// i2c_wr8(sd, EDID_LEN2, edid_len >> 8);

	if (edid->blocks == 0) {
		state->edid_blocks_written = 0;
		return 0;
	}

	// for (i = 0; i < edid_len; i += EDID_BLOCK_SIZE)
	// 	i2c_wr(sd, EDID_RAM + i, edid->edid + i, EDID_BLOCK_SIZE);

	state->edid_blocks_written = edid->blocks;

	cec_s_phys_addr(state->cec_adap, pa, false);

	// if (tx_5v_power_present(sd))
	// 	tc358743_enable_edid(sd);

	return 0;
}

static int tc358743_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				    struct v4l2_event_subscription *sub)
{
	pr_info(TAG "  $$ subscibe_event");

	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subdev_subscribe(sd, fh, sub);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
	default:
		return -EINVAL;
	}
}


static int tc358743_isr(struct v4l2_subdev *sd, u32 status, bool *handled)
{
	pr_info(TAG "  $$ isr");

	// u16 intstatus = i2c_rd16(sd, INTSTATUS);

	// v4l2_dbg(1, debug, sd, "%s: IntStatus = 0x%04x\n", __func__, intstatus);

	// if (intstatus & MASK_HDMI_INT) {
	// 	u8 hdmi_int0 = i2c_rd8(sd, HDMI_INT0);
	// 	u8 hdmi_int1 = i2c_rd8(sd, HDMI_INT1);

	// 	if (hdmi_int0 & MASK_I_MISC)
	// 		tc358743_hdmi_misc_int_handler(sd, handled);
	// 	if (hdmi_int1 & MASK_I_CBIT)
	// 		tc358743_hdmi_cbit_int_handler(sd, handled);
	// 	if (hdmi_int1 & MASK_I_CLK)
	// 		tc358743_hdmi_clk_int_handler(sd, handled);
	// 	if (hdmi_int1 & MASK_I_SYS)
	// 		tc358743_hdmi_sys_int_handler(sd, handled);
	// 	if (hdmi_int1 & MASK_I_AUD)
	// 		tc358743_hdmi_audio_int_handler(sd, handled);

	// 	// i2c_wr16(sd, INTSTATUS, MASK_HDMI_INT);
	// 	intstatus &= ~MASK_HDMI_INT;
	// }

// #ifdef CONFIG_VIDEO_TC358743_CEC
// 	if (intstatus & (MASK_CEC_RINT | MASK_CEC_TINT)) {
// 		tc358743_cec_handler(sd, intstatus, handled);
// 		i2c_wr16(sd, INTSTATUS,
// 			 intstatus & (MASK_CEC_RINT | MASK_CEC_TINT));
// 		intstatus &= ~(MASK_CEC_RINT | MASK_CEC_TINT);
// 	}
// #endif

// 	if (intstatus & MASK_CSI_INT) {
// 		u32 csi_int = i2c_rd32(sd, CSI_INT);

// 		if (csi_int & MASK_INTER)
// 			tc358743_csi_err_int_handler(sd, handled);

// 		i2c_wr16(sd, INTSTATUS, MASK_CSI_INT);
// 	}

// 	intstatus = i2c_rd16(sd, INTSTATUS);
// 	if (intstatus) {
// 		v4l2_dbg(1, debug, sd,
// 				"%s: Unhandled IntStatus interrupts: 0x%02x\n",
// 				__func__, intstatus);
// 	}

	return 0;
}

static int tc358743_log_status(struct v4l2_subdev *sd)
{
	pr_info(TAG "  $$ log_status");
	return 0;
}

static const struct v4l2_subdev_core_ops tc358743_core_ops = {
	// .log_status = tc358743_log_status,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = tc358743_g_register,
	.s_register = tc358743_s_register,
#endif
	.interrupt_service_routine = tc358743_isr,
	.subscribe_event = tc358743_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops tc358743_video_ops = {
	.g_input_status = tc358743_g_input_status,
	.s_dv_timings = tc358743_s_dv_timings,
	.g_dv_timings = tc358743_g_dv_timings,
	.query_dv_timings = tc358743_query_dv_timings,
	.s_stream = tc358743_s_stream,
};

static const struct v4l2_subdev_pad_ops tc358743_pad_ops = {
	.enum_mbus_code = tc358743_enum_mbus_code,
	.set_fmt = tc358743_set_fmt,
	.get_fmt = tc358743_get_fmt,
	.get_edid = tc358743_g_edid,
	.set_edid = tc358743_s_edid,
	.enum_dv_timings = tc358743_enum_dv_timings,
	.dv_timings_cap = tc358743_dv_timings_cap,
	.get_mbus_config = tc358743_get_mbus_config,
};

static const struct v4l2_subdev_ops tc358743_ops = {
	.core = &tc358743_core_ops,
	.video = &tc358743_video_ops,
	.pad = &tc358743_pad_ops,
};

static int tc358743_link_setup(struct media_entity *entity,
			       const struct media_pad *local,
			       const struct media_pad *remote, u32 flags)
{
	return 0;
}

static const struct media_entity_operations tc358743_sd_media_ops = {
	.link_setup = tc358743_link_setup,
};

static int tc358743_probe_of(struct tc358743_state *state)
{
	struct device *dev = &state->i2c_client->dev;
	struct v4l2_fwnode_endpoint endpoint = { .bus_type = 0 };
	struct device_node *ep;
	struct clk *refclk;
	u32 bps_pr_lane;
	int ret;

// pr_info(TAG "bb 1, 0x%x", state);
// pr_info(TAG "bb 1, 0x%x", state->i2c_client);
// pr_info(TAG "bb 1, 0x%x", dev);
// 	refclk = devm_clk_get(dev, "refclk");
// pr_info(TAG "bb 1, %d", refclk);
// 	if (IS_ERR(refclk)) {
// 		if (PTR_ERR(refclk) != -EPROBE_DEFER)
// 			dev_err(dev, "failed to get refclk: %ld\n",
// 				PTR_ERR(refclk));
// 		return PTR_ERR(refclk);
	// }

pr_info(TAG "bb 2");
	ep = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!ep) {
		dev_err(dev, "missing endpoint node\n");
		return -EINVAL;
	}

pr_info(TAG "bb 3");
	ret = v4l2_fwnode_endpoint_alloc_parse(of_fwnode_handle(ep), &endpoint);
	if (ret) {
		dev_err(dev, "failed to parse endpoint\n");
		goto put_node;
	}

pr_info(TAG "bb 4");
	if (endpoint.bus_type != V4L2_MBUS_CSI2_DPHY ||
	    endpoint.bus.mipi_csi2.num_data_lanes == 0 ||
	    endpoint.nr_of_link_frequencies == 0) {
		dev_err(dev, "missing CSI-2 properties in endpoint\n");
		ret = -EINVAL;
		goto free_endpoint;
	}

pr_info(TAG "bb 5 - lanes %d", endpoint.bus.mipi_csi2.num_data_lanes);
	if (endpoint.bus.mipi_csi2.num_data_lanes > 4) {
		dev_err(dev, "invalid number of lanes\n");
		ret = -EINVAL;
		goto free_endpoint;
	}

	state->bus = endpoint.bus.mipi_csi2;

pr_info(TAG "bb 6");
	ret = clk_prepare_enable(refclk);
	if (ret) {
		dev_err(dev, "Failed! to enable clock\n");
		goto free_endpoint;
	}

pr_info(TAG "bb 7");
	state->pdata.refclk_hz = clk_get_rate(refclk);
	state->pdata.ddc5v_delay = DDC5V_DELAY_100_MS;
	state->pdata.enable_hdcp = false;
	/* A FIFO level of 16 should be enough for 2-lane 720p60 at 594 MHz. */
	state->pdata.fifo_level = 16;
	/*
	 * The PLL input clock is obtained by dividing refclk by pll_prd.
	 * It must be between 6 MHz and 40 MHz, lower frequency is better.
	 */
	// switch (state->pdata.refclk_hz) {
	// case 26000000:
	// case 27000000:
	// case 42000000:
	// 	state->pdata.pll_prd = state->pdata.refclk_hz / 6000000;
	// 	break;
	// default:
	// 	dev_err(dev, "unsupported refclk rate: %u Hz\n",
	// 		state->pdata.refclk_hz);
	// 	goto disable_clk;
	// }

	/*
	 * The CSI bps per lane must be between 62.5 Mbps and 1 Gbps.
	 * The default is 594 Mbps for 4-lane 1080p60 or 2-lane 720p60.
	 */
	// bps_pr_lane = 2 * endpoint.link_frequencies[0];
	// if (bps_pr_lane < 62500000U || bps_pr_lane > 1000000000U) {
	// 	dev_err(dev, "unsupported bps per lane: %u bps\n", bps_pr_lane);
	// 	ret = -EINVAL;
	// 	goto disable_clk;
	// }

	// /* The CSI speed per lane is refclk / pll_prd * pll_fbd */
	// state->pdata.pll_fbd = bps_pr_lane /
	// 		       state->pdata.refclk_hz * state->pdata.pll_prd;

	/*
	 * FIXME: These timings are from REF_02 for 594 Mbps per lane (297 MHz
	 * link frequency). In principle it should be possible to calculate
	 * them based on link frequency and resolution.
	 */
	// if (bps_pr_lane != 594000000U)
	// 	dev_warn(dev, "untested bps per lane: %u bps\n", bps_pr_lane);
	// state->pdata.lineinitcnt = 0xe80;
	// state->pdata.lptxtimecnt = 0x003;
	// /* tclk-preparecnt: 3, tclk-zerocnt: 20 */
	// state->pdata.tclk_headercnt = 0x1403;
	// state->pdata.tclk_trailcnt = 0x00;
	// /* ths-preparecnt: 3, ths-zerocnt: 1 */
	// state->pdata.ths_headercnt = 0x0103;
	// state->pdata.twakeup = 0x4882;
	// state->pdata.tclk_postcnt = 0x008;
	// state->pdata.ths_trailcnt = 0x2;
	// state->pdata.hstxvregcnt = 0;

	// state->reset_gpio = devm_gpiod_get_optional(dev, "reset",
	// 					    GPIOD_OUT_LOW);
	// if (IS_ERR(state->reset_gpio)) {
	// 	dev_err(dev, "failed to get reset gpio\n");
	// 	ret = PTR_ERR(state->reset_gpio);
	// 	goto disable_clk;
	// }

	// if (state->reset_gpio)
	// 	tc358743_gpio_reset(state);

	ret = 0;
pr_info(TAG "bb 8 return ok");
	goto free_endpoint;

disable_clk:
	clk_disable_unprepare(refclk);
free_endpoint:
	v4l2_fwnode_endpoint_free(&endpoint);
put_node:
	of_node_put(ep);
	return ret;
}
// #else
// static inline int tc358743_probe_of(struct tc358743_state *state)
// {
// 	return -ENODEV;
// }
// #endif

static int i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{



	static struct v4l2_dv_timings default_timing = // V4L2_DV_BT_CEA_640X480P59_94;
	{
	.type = V4L2_DV_BT_656_1120,
	V4L2_INIT_BT_TIMINGS(640, 480, 0, 0,
		25175000 / 2, // $$
		16, 96, 48, 10, 2, 33, 0, 0, 0,
		V4L2_DV_BT_STD_DMT | V4L2_DV_BT_STD_CEA861,
		V4L2_DV_FL_HAS_CEA861_VIC, { 0, 0 }, 1)
};

	struct tc358743_state *state;
	struct tc358743_platform_data *pdata = client->dev.platform_data;
	struct v4l2_subdev *sd;
	// u16 irq_mask = MASK_HDMI_MSK | MASK_CSI_MSK;
	int err;



	tc358748_i2c_client = client;

	pr_info(TAG "probe driver I2C Toshiba TC358748 - 0x%02x address\n", client->addr);

	// tc358748_setup(); // $$






	// if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
	// 	return -EIO;
	v4l_dbg(1, debug, client, "chip found @ 0x%x (%s)\n",
		client->addr << 1, client->adapter->name);

	state = devm_kzalloc(&client->dev, sizeof(struct tc358743_state),
			GFP_KERNEL);
	if (!state)
		return -ENOMEM;
pr_info(TAG "aa");

	state->i2c_client = client;

	/* platform data */
	if (pdata) {
pr_info(TAG "aa1a");
		state->pdata = *pdata;
		state->bus.flags = V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	} else {
pr_info(TAG "aa1b");
		err = tc358743_probe_of(state);
pr_info(TAG "aa1b %d %d", err, ENODEV);
		if (err == -ENODEV)
			v4l_err(client, "No platform data!\n");
		if (err)
			return err;
	}
pr_info(TAG "aa2");

	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &tc358743_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;

	/* i2c access */
	// if ((i2c_rd16(sd, CHIPID) & MASK_CHIPID) != 0) {
	// 	v4l2_info(sd, "not a TC358743 on address 0x%x\n",
	// 		  client->addr << 1);
	// 	return -ENODEV;
	// }

	/* control handlers */
	v4l2_ctrl_handler_init(&state->hdl, 3);

	state->detect_tx_5v_ctrl = v4l2_ctrl_new_std(&state->hdl, NULL,
			V4L2_CID_DV_RX_POWER_PRESENT, 0, 1, 0, 0);

	// /* custom controls */
	// state->audio_sampling_rate_ctrl = v4l2_ctrl_new_custom(&state->hdl,
	// 		&tc358743_ctrl_audio_sampling_rate, NULL);

	// state->audio_present_ctrl = v4l2_ctrl_new_custom(&state->hdl,
	// 		&tc358743_ctrl_audio_present, NULL);

pr_info(TAG "aa3");

	sd->ctrl_handler = &state->hdl;
	if (state->hdl.error) {
		err = state->hdl.error;
		goto err_hdl;
	}

pr_info(TAG "aa4");
	// if (tc358743_update_controls(sd)) {
	// 	err = -ENODEV;
	// 	goto err_hdl;
	// }

	state->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.ops = &tc358743_sd_media_ops;
	sd->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	err = media_entity_pads_init(&sd->entity, 1, &state->pad);
	if (err < 0)
		goto err_hdl;

pr_info(TAG "aa5");

	state->mbus_fmt_code = MEDIA_BUS_FMT_RGB888_1X24;

	sd->dev = &client->dev;
	err = v4l2_async_register_subdev(sd);
	if (err < 0)
		goto err_hdl;

pr_info(TAG "aa6");

	mutex_init(&state->confctl_mutex);

	// INIT_DELAYED_WORK(&state->delayed_work_enable_hotplug,
	// 		tc358743_delayed_work_enable_hotplug);

#ifdef CONFIG_VIDEO_TC358743_CEC
	state->cec_adap = cec_allocate_adapter(&tc358743_cec_adap_ops,
		state, dev_name(&client->dev),
		CEC_CAP_DEFAULTS | CEC_CAP_MONITOR_ALL, CEC_MAX_LOG_ADDRS);
	if (IS_ERR(state->cec_adap)) {
		err = PTR_ERR(state->cec_adap);
		goto err_hdl;
	}
	irq_mask |= MASK_CEC_RMSK | MASK_CEC_TMSK;
#endif

	// tc358743_initial_setup(sd);
pr_info(TAG "aa7");

	tc358743_s_dv_timings(sd, &default_timing);

	// tc358743_set_csi_color_space(sd);

	// tc358743_init_interrupts(sd);

	// if (state->i2c_client->irq) {
	// 	err = devm_request_threaded_irq(&client->dev,
	// 					state->i2c_client->irq,
	// 					NULL, tc358743_irq_handler,
	// 					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
	// 					"tc358743", state);
	// 	if (err)
	// 		goto err_work_queues;
	// } else {
	// 	INIT_WORK(&state->work_i2c_poll,
	// 		  tc358743_work_i2c_poll);
	// 	timer_setup(&state->timer, tc358743_irq_poll_timer, 0);
	// 	state->timer.expires = jiffies +
	// 			       msecs_to_jiffies(POLL_INTERVAL_MS);
	// 	add_timer(&state->timer);
	// }

	// err = cec_register_adapter(state->cec_adap, &client->dev);
	// if (err < 0) {
	// 	pr_err("%s: failed to register the cec device\n", __func__);
	// 	cec_delete_adapter(state->cec_adap);
	// 	state->cec_adap = NULL;
	// 	goto err_work_queues;
	// }

	// tc358743_enable_interrupts(sd, tx_5v_power_present(sd));
	// i2c_wr16(sd, INTMASK, ~irq_mask);

pr_info(TAG "aa8");

	err = v4l2_ctrl_handler_setup(sd->ctrl_handler);
	if (err)
		goto err_work_queues;

pr_info(TAG "aa9");

	v4l2_info(sd, "%s found @ 0x%x (%s)\n", client->name,
		  client->addr << 0, client->adapter->name);

	return 0;

err_work_queues:
	cec_unregister_adapter(state->cec_adap);
	if (!state->i2c_client->irq)
		flush_work(&state->work_i2c_poll);
	cancel_delayed_work(&state->delayed_work_enable_hotplug);
	mutex_destroy(&state->confctl_mutex);
err_hdl:
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&state->hdl);
	return err;
}

static int i2c_remove(struct i2c_client *client)
{
	pr_info(TAG "remove driver I2C Toshiba TC358748 - 0x%02x address\n", client->addr);


	// 	/* TXOPTIONCNTRL */
	// if (!i2c_write_reg32(tc358748_i2c_client, TXOPTIONCNTRL, 0))
	// {
	// 	pr_err(TAG "Can't write TXOPTIONCNTRL");
	// 	return false;
	// }
	// pr_info(TAG "TXOPTIONCNTRL (0x%04x) = 0", TXOPTIONCNTRL);

	// 	/* STARTCNTRL */
	// if (!i2c_write_reg32(tc358748_i2c_client, STARTCNTRL, 0))
	// {
	// 	pr_err(TAG "Can't write STARTCNTRL");
	// 	return false;
	// }
	// pr_info(TAG "STARTCNTRL (0x%04x) = 0", STARTCNTRL);

	// 	/* CSI_START */
	// if (!i2c_write_reg32(tc358748_i2c_client, CSI_START, 0))
	// {
	// 	pr_err(TAG "Can't write CSI_START");
	// 	return false;
	// }
	// pr_info(TAG "CSI_START (0x%04x) = 0", CSI_START);



	// struct v4l2_subdev *sd = i2c_get_clientdata(client);
	// struct tc358743_state *state = to_state(sd);

	// if (!state->i2c_client->irq) {
	// 	del_timer_sync(&state->timer);
	// 	flush_work(&state->work_i2c_poll);
	// }
	// cancel_delayed_work_sync(&state->delayed_work_enable_hotplug);
	// cec_unregister_adapter(state->cec_adap);
	// v4l2_async_unregister_subdev(sd);
	// v4l2_device_unregister_subdev(sd);
	// mutex_destroy(&state->confctl_mutex);
	// media_entity_cleanup(&sd->entity);
	// v4l2_ctrl_handler_free(&state->hdl);




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
