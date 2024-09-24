// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2019 Enrico Scholz <enrico.scholz@sigma-chemnitz.de>
 * Copyright (C) 2021 PHYTEC Messtechnik GmbH
 * Author: Enrico Scholz <enrico.scholz@sigma-chemnitz.de>
 * Author: Stefan Riedmueller <s.riedmueller@phytec.de>
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>

#include <linux/of_graph.h>

#include <linux/v4l2-mediabus.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ctrls.h>

#include "../drivers/media/i2c/vvsensor.h"

#include "tc358748_i2c.h"

#define AR0521_MAX_LINK_FREQ		600000000ULL

// #define AR0521_DEF_WIDTH		2592
// #define AR0521_DEF_HEIGHT		1944
#define AR0521_DEF_WIDTH		640
#define AR0521_DEF_HEIGHT		480

struct ar0521_format {
	unsigned int code;
	unsigned int bpp;
};

static const struct ar0521_format ar0521_col_formats = 
	{
		// .code	= MEDIA_BUS_FMT_SGRBG8_1X8,
		.code = MEDIA_BUS_FMT_RGB888_1X24,
		.bpp	= 8,
	// },
};

struct limit_range {
	unsigned long min;
	unsigned long max;
};

struct ar0521_sensor_limits {
	struct limit_range x;
	struct limit_range y;
	struct limit_range hlen;
	struct limit_range vlen;
	struct limit_range hblank;
	struct limit_range vblank;
	struct limit_range ext_clk;
};

struct ar0521_businfo {
	unsigned int num_lanes;
	unsigned int flags;
	const s64 *link_freqs;

	u16 t_hs_prep;
	u16 t_hs_zero;
	u16 t_hs_trail;
	u16 t_clk_prep;
	u16 t_clk_zero;
	u16 t_clk_trail;
	u16 t_bgap;
	u16 t_clk_pre;
	u16 t_clk_post_msbs;
	u16 t_lpx;
	u16 t_wakeup;
	u16 t_clk_post;
	u16 t_hs_exit;
	u16 t_init;
	bool cont_tx_clk;
	bool vreg_mode;
};

struct ar0521_register {
	u16 reg;
	u16 val;
};

struct ar0521 {
	struct v4l2_subdev subdev;
	struct v4l2_ctrl_handler ctrls;
	struct media_pad pad;

	struct v4l2_mbus_framefmt fmt;
	struct v4l2_rect crop;
	unsigned int bpp;
	unsigned int w_skip;
	unsigned int h_skip;
	unsigned int hlen;
	unsigned int vlen;

	struct ar0521_businfo info;
	struct ar0521_sensor_limits limits;

	// const struct ar0521_format *formats;
	// unsigned int num_fmts;

	struct v4l2_ctrl *exp_ctrl;
	struct v4l2_ctrl *vblank_ctrl;
	struct v4l2_ctrl *hblank_ctrl;

	struct vvcam_mode_info_s vvcam_mode;
	unsigned int vvcam_cur_mode_index;

#ifdef DEBUG
	struct dentry *debugfs_root;
	bool manual_pll;
#endif /* ifdef DEBUG */

	struct clk *extclk;
	struct gpio_desc *reset_gpio;

	struct mutex lock;

	int power_user;
	int trigger_pin;
	int trigger;
	bool is_streaming;
};

struct priv_ioctl {
	u32 idx;
	const char * const name;
};

static inline struct ar0521 *to_ar0521(struct v4l2_subdev *sd);
static inline int bpp_to_index(unsigned int bpp);
static int ar0521_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel);
static int ar0521_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *format);

static long ar0521_priv_ioctl(struct v4l2_subdev *sd, unsigned int cmd,
			      void *arg)
{
	return 0;
}

static inline struct ar0521 *to_ar0521(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ar0521, subdev);
}

static inline unsigned int index_to_bpp(int index)
{
	return index * 2 + 8;
}

static inline int bpp_to_index(unsigned int bpp)
{
	return (bpp - 8) / 2;
}

/* V4L2 subdev core ops */
static int ar0521_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ar0521_s_register(struct v4l2_subdev *sd,
			     const struct v4l2_dbg_register *reg)
{
	struct ar0521 *sensor = to_ar0521(sd);

	dev_dbg(sd->dev, "%s\n", __func__);

	return ar0521_write(sensor, reg->reg, reg->val);
}

static int ar0521_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct ar0521 *sensor = to_ar0521(sd);

	dev_dbg(sd->dev, "%s\n", __func__);

	return ar0521_read(sensor, reg->reg, (u16 *)&reg->val);
}
#endif

/* V4L2 subdev video ops */
static int ar0521_s_stream(struct v4l2_subdev *sd, int enable)
{
pr_info("--------------------- s_stream!!!!!!!!!!!!");

	return 0;
}

static int ar0521_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *interval)
{
	struct ar0521 *sensor = to_ar0521(sd);
	// unsigned long pix_freq;
	int index;

	// mutex_lock(&sensor->lock);

	index = bpp_to_index(sensor->bpp);

	// pix_freq = sensor->pll[index].pix_freq;

	// interval->interval.numerator = 10;
	// interval->interval.denominator = div_u64(pix_freq * 10ULL,
	// 					 sensor->vlen * sensor->hlen);

	// mutex_unlock(&sensor->lock);

	return 0;
}

static struct v4l2_rect *ar0521_get_pad_crop(struct ar0521 *sensor,
					     struct v4l2_subdev_state *state,
					     unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(&sensor->subdev, state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &sensor->crop;
	default:
		return NULL;
	}
}

static struct v4l2_mbus_framefmt *ar0521_get_pad_fmt(struct ar0521 *sensor,
					    struct v4l2_subdev_state *state,
					    unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&sensor->subdev, state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &sensor->fmt;
	default:
		return NULL;
	}
}

static unsigned int ar0521_find_skipfactor(unsigned int input,
					   unsigned int output)
{
	int i;

	/*
	 * We need to determine a matching supported power-of-two skip
	 * factor. If no exact match is found. the next bigger matching
	 * factor is returned.
	 * Supported factors are:
	 * No Skip
	 * Skip 2
	 * Skip 4
	 */

pr_info("--------------------- in/out %d %d", input, output);

	for (i = 0; i < 2; i++)
		if ((input >> i) <= output)
			break;

	return (1 << i);
}

/* V4L2 subdev pad ops */
static int ar0521_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	// struct ar0521 *sensor = to_ar0521(sd);

	// if (code->index < sensor->num_fmts) {
		// code->code = sensor->formats[code->index].code;
		code->code = 8;
		return 0;
	// } else {
	// 	return -EINVAL;
	// }
}

static int ar0521_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct ar0521 *sensor = to_ar0521(sd);
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_rect *crop;
	int ret = 0;

pr_info("--------------------- frame_size start: index %d", fse->index);
	mutex_lock(&sensor->lock);

	fmt = ar0521_get_pad_fmt(sensor, state, fse->pad, fse->which);
	crop = ar0521_get_pad_crop(sensor, state, fse->pad, fse->which);

	if (fse->index >= 4 || fse->code != fmt->code) {
pr_info("--------------------- frame_size   err: index %d, code %d", fse->index, fse->code);
		ret = -EINVAL;
		goto out;
	}

	fse->min_width = crop->width / (1u << fse->index);
	fse->max_width = fse->min_width;
	fse->min_height = crop->height / (1u << fse->index);
	fse->max_height = fse->min_height;

pr_info("--------------------- frame_size  crop %d %d %d %d",
		crop->left, crop->top, crop->width, crop->height);
pr_info("--------------------- frame_size  min max %d %d %d %d", 
		fse->min_width, fse->max_width, fse->min_height, fse->max_height);

	if (fse->min_width <= 1 || fse->min_height <= 1)
	{
pr_info("--------------------- frame_size   err: min_width %d, min_height %d",
				fse->min_width, fse->min_height);
		ret = -EINVAL;
	}

out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static void ar0521_update_blankings(struct ar0521 *sensor)
{
	const struct ar0521_sensor_limits *limits = &sensor->limits;
	unsigned int width = sensor->fmt.width;
	unsigned int height = sensor->fmt.height;
	unsigned int hblank_min, hblank_max;
	unsigned int vblank_min, vblank_max;
	unsigned int hblank_value, hblank_default;
	unsigned int vblank_value, vblank_default;

	hblank_min = limits->hblank.min;
	if (width + limits->hblank.min < limits->hlen.min)
		hblank_min = limits->hlen.min - width;

	vblank_min = limits->vblank.min;
	if (height + limits->vblank.min < limits->vlen.min)
		vblank_min = limits->vlen.min - height;

	hblank_max = limits->hlen.max - width;
	vblank_max = limits->vlen.max - height;

	hblank_value = sensor->hblank_ctrl->cur.val;
	hblank_default = sensor->hblank_ctrl->default_value;

	vblank_value = sensor->vblank_ctrl->cur.val;
	vblank_default = sensor->vblank_ctrl->default_value;

	if (hblank_value < hblank_min)
		__v4l2_ctrl_s_ctrl(sensor->hblank_ctrl, hblank_min);

	if (hblank_value > hblank_max)
		__v4l2_ctrl_s_ctrl(sensor->hblank_ctrl, hblank_max);

	if (vblank_value < vblank_min)
		__v4l2_ctrl_s_ctrl(sensor->vblank_ctrl, vblank_min);

	if (vblank_value > vblank_max)
		__v4l2_ctrl_s_ctrl(sensor->vblank_ctrl, vblank_max);

	__v4l2_ctrl_modify_range(sensor->hblank_ctrl, hblank_min, hblank_max,
				 sensor->hblank_ctrl->step,
				 hblank_min);

	__v4l2_ctrl_modify_range(sensor->vblank_ctrl, vblank_min, vblank_max,
				 sensor->vblank_ctrl->step,
				 vblank_min);


	/*
	 * If the previous value was equal the previous default value, readjust
	 * the updated value to the new default value as well.
	 */
	if (hblank_value == hblank_default)
		__v4l2_ctrl_s_ctrl(sensor->hblank_ctrl, hblank_min);

	if (vblank_value == vblank_default)
		__v4l2_ctrl_s_ctrl(sensor->vblank_ctrl, vblank_min);
}

static int ar0521_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *format)
{
	struct ar0521 *sensor = to_ar0521(sd);
	// const struct ar0521_format *sensor_format;
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_rect *crop;
	unsigned int width, height;
	unsigned int w_skip, h_skip;

	dev_dbg(sd->dev, "%s\n", __func__);
pr_info("--------------------- set_fmt start");

	mutex_lock(&sensor->lock);

	if (sensor->is_streaming &&
	    format->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		mutex_unlock(&sensor->lock);
		return -EBUSY;
	}

	fmt = ar0521_get_pad_fmt(sensor, state, format->pad, format->which);
	crop = ar0521_get_pad_crop(sensor, state, format->pad,
				   V4L2_SUBDEV_FORMAT_ACTIVE);

	// if (sensor->model == AR0521_MODEL_COLOR)
	// 	fmt->colorspace = V4L2_COLORSPACE_RAW;
	// else
		fmt->colorspace = V4L2_COLORSPACE_SRGB;

	fmt->field = V4L2_FIELD_NONE;
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							  fmt->colorspace,
							  fmt->ycbcr_enc);

	// sensor_format = ar0521_col_formats; // ar0521_find_format(sensor, format->format.code);
	// fmt->code = sensor_format->code;

// fmt->code = MEDIA_BUS_FMT_Y8_1X8;
// fmt->code = MEDIA_BUS_FMT_SGRBG8_1X8;
fmt->code = MEDIA_BUS_FMT_RGB888_1X24;

	width = clamp_t(unsigned int, format->format.width,
			1, crop->width);
	height = clamp_t(unsigned int, format->format.height,
			 1, crop->height);

	w_skip = ar0521_find_skipfactor(crop->width, width);
	h_skip = ar0521_find_skipfactor(crop->height, height);

	fmt->width = crop->width / w_skip;
	fmt->height = crop->height / h_skip;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		// sensor->bpp = sensor_format->bpp;
sensor->bpp = 8;
		sensor->w_skip = w_skip;
		sensor->h_skip = h_skip;

		// ar0521_update_blankings(sensor);
sensor->hlen = 800;
		// sensor->hlen = fmt->width + sensor->hblank_ctrl->cur.val;
		// sensor->vlen = fmt->height + sensor->vblank_ctrl->cur.val;
sensor->vlen = 525;
	}

pr_info("--------------------- set_fmt end");
	format->format = *fmt;

	mutex_unlock(&sensor->lock);
	return 0;
}

static int ar0521_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *format)
{
	struct ar0521 *sensor = to_ar0521(sd);
	struct v4l2_mbus_framefmt *fmt;

	dev_dbg(sd->dev, "%s\n", __func__);

	mutex_lock(&sensor->lock);

	fmt = ar0521_get_pad_fmt(sensor, state, format->pad, format->which);
	format->format = *fmt;
pr_info("--------------------- get_fmt width %d", fmt->width);
pr_info("--------------------- get_fmt height %d", fmt->height);
pr_info("--------------------- get_fmt code %d", fmt->code);
pr_info("--------------------- get_fmt field %d", fmt->field);
pr_info("--------------------- get_fmt colorspace %d", fmt->colorspace);
pr_info("--------------------- get_fmt xfer_func %d", fmt->xfer_func);
pr_info("--------------------- get_fmt flags %d", fmt->flags);

	mutex_unlock(&sensor->lock);

	return 0;
}

static int ar0521_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	return 0;
}

static int ar0521_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	return 0;
}

static int ar0521_get_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				  struct v4l2_mbus_config *cfg)
{
	struct ar0521 *sensor = to_ar0521(sd);

	cfg->flags = sensor->info.flags;
	cfg->type = V4L2_MBUS_CSI2_DPHY;

	return 0;
}

static const struct v4l2_subdev_core_ops ar0521_subdev_core_ops = {
	.s_power		= ar0521_s_power,
	.ioctl			= ar0521_priv_ioctl,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.s_register		= ar0521_s_register,
	.g_register		= ar0521_g_register,
#endif
};

static const struct v4l2_subdev_video_ops ar0521_subdev_video_ops = {
	.s_stream		= ar0521_s_stream,
	.g_frame_interval	= ar0521_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops ar0521_subdev_pad_ops = {
	.enum_mbus_code  = ar0521_enum_mbus_code,
	.enum_frame_size = ar0521_enum_frame_size,
	.set_fmt         = ar0521_set_fmt,
	.get_fmt         = ar0521_get_fmt,
	.set_selection   = ar0521_set_selection,
	.get_selection   = ar0521_get_selection,
	.get_mbus_config = ar0521_get_mbus_config,
};

static const struct v4l2_subdev_ops ar0521_subdev_ops = {
	.core  = &ar0521_subdev_core_ops,
	.video = &ar0521_subdev_video_ops,
	.pad   = &ar0521_subdev_pad_ops,
};

static const struct media_entity_operations ar0521_entity_ops = {
	.get_fwnode_pad		= v4l2_subdev_get_fwnode_pad_1_to_1,
};

static void ar0521_set_defaults(struct ar0521 *sensor)
{
	sensor->limits = (struct ar0521_sensor_limits) {
					/* mim		max      */
		.x			= {0,		2603     },
		.y			= {0,		1955     },
		.hlen			= {3080,	65532    },
		.vlen			= {48,		65535    },
		.hblank			= {240,		65535    },
		.vblank			= {28,		65535    },
		.ext_clk		= {5000000,	64000000 },
	};

	sensor->crop.left = 0;
	sensor->crop.top = 0;
	sensor->crop.width = AR0521_DEF_WIDTH;
	sensor->crop.height = AR0521_DEF_HEIGHT;

	sensor->fmt.width = AR0521_DEF_WIDTH;
	sensor->fmt.height = AR0521_DEF_HEIGHT;
	sensor->fmt.field = V4L2_FIELD_NONE;
	// sensor->fmt.colorspace = V4L2_COLORSPACE_RAW;
sensor->fmt.colorspace = V4L2_COLORSPACE_SRGB;

	// if (sensor->model == AR0521_MODEL_MONOCHROME) {
	// 	sensor->formats = ar0521_mono_formats;
	// 	sensor->num_fmts = ARRAY_SIZE(ar0521_mono_formats);
	// } else
	{
		// sensor->formats = ar0521_col_formats;
		// sensor->num_fmts = ARRAY_SIZE(ar0521_col_formats);
	}

	// sensor->fmt.code = sensor->formats[sensor->num_fmts - 1].code;
	// sensor->bpp = sensor->formats[sensor->num_fmts - 1].bpp;
	sensor->fmt.code = ar0521_col_formats.code;
	sensor->bpp = ar0521_col_formats.bpp;

	sensor->w_skip = 1;
	sensor->h_skip = 1;
	sensor->hlen = sensor->limits.hlen.min;
	sensor->vlen = sensor->fmt.height + sensor->limits.vblank.min;

#ifdef DEBUG
	sensor->manual_pll = false;
#endif /* ifdef DEBUG */
}

static int ar0521_subdev_registered(struct v4l2_subdev *sd)
{
	struct ar0521 *sensor = to_ar0521(sd);

	ar0521_set_defaults(sensor);

#ifdef DEBUG
	ar0521_debugfs_init(sensor);
#endif /* ifdef DEBUG */

pr_info("-------- ar0521_subdev_registered");  // $$

	return 0;
}

static const struct v4l2_subdev_internal_ops ar0521_subdev_internal_ops = {
	.registered		= ar0521_subdev_registered,
};

static int ar0521_parse_endpoint(struct device *dev, struct ar0521 *sensor,
				 struct fwnode_handle *ep)
{
	struct v4l2_fwnode_endpoint buscfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	u64 *link_freqs;
	// unsigned long ext_freq = clk_get_rate(sensor->extclk);
	// unsigned int tmp;
	// int i;
	int ret;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &buscfg);
	if (ret) {
		dev_err(dev, "Failed to parse MIPI endpoint (%d)\n", ret);
		return ret;
	}

	sensor->info.num_lanes = buscfg.bus.mipi_csi2.num_data_lanes;
	sensor->info.flags = buscfg.bus.mipi_csi2.flags;
	sensor->info.flags |= V4L2_MBUS_CSI2_CHANNEL_0;
pr_info("--------------------- num lanes %d", sensor->info.num_lanes);

	switch (sensor->info.num_lanes) {
	case 2:
		sensor->info.flags |= V4L2_MBUS_CSI2_2_LANE;
		break;
	case 4:
		sensor->info.flags |= V4L2_MBUS_CSI2_4_LANE;
		break;
	default:
		dev_err(dev, "Wrong number of lanes configured");
		ret = -EINVAL;
		goto out;
	}

	if (buscfg.nr_of_link_frequencies != 1) {
		dev_err(dev, "MIPI link frequency required\n");
		ret = -EINVAL;
		goto out;
	}

	if (buscfg.link_frequencies[0] > AR0521_MAX_LINK_FREQ) {
		dev_err(dev, "MIPI link frequency exceeds maximum\n");
		ret = -EINVAL;
		goto out;
	}

	link_freqs = devm_kcalloc(dev, 3, sizeof(*sensor->info.link_freqs),
				  GFP_KERNEL);
	if (!link_freqs) {
		ret = -ENOMEM;
		goto out;
	}

out:
	v4l2_fwnode_endpoint_free(&buscfg);
	return ret;
}

static int ar0521_of_probe(struct device *dev, struct ar0521 *sensor)
{
	struct fwnode_handle *ep;
	struct clk *clk;
	struct gpio_desc *gpio;
	int ret;

	clk = devm_clk_get(dev, "ext");
	ret = PTR_ERR_OR_ZERO(clk);
	if (ret == -EPROBE_DEFER)
		return ret;
	if (ret < 0) {
		dev_err(dev, "Failed to get external clock (%d)\n", ret);
		return ret;
	}

	sensor->extclk = clk;

	gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	ret = PTR_ERR_OR_ZERO(gpio);
	if (ret < 0) {
		dev_err(dev, "Failed to get reset gpio (%d)\n", ret);
		return ret;
	}

	sensor->reset_gpio = gpio;

	ep = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);

	if (!ep) {
		dev_err(dev, "Failed to find endpoint\n");
		return -ENODEV;
	}

	ret = ar0521_parse_endpoint(dev, sensor, ep);

	fwnode_handle_put(ep);
	return ret;
}

static int ar0521_probe(struct i2c_client *i2c,
			const struct i2c_device_id *did)
{
	struct ar0521 *sensor;
	struct v4l2_subdev *sd;
	int ret;

	if (!tc358748_setup(i2c))
	{
		pr_err("Can't initialize TC358748 through I2C");
		return -1;
	}

	sensor = devm_kzalloc(&i2c->dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	dev_info(&i2c->dev, "Probing ar0521 Driver\n");

	sd = &sensor->subdev;
	// sensor->model = did->driver_data;

	ret = ar0521_of_probe(&i2c->dev, sensor);
	if (ret)
		return ret;
	mutex_init(&sensor->lock);

	v4l2_i2c_subdev_init(sd, i2c, &ar0521_subdev_ops);

	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->internal_ops = &ar0521_subdev_internal_ops;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sd->entity.ops = &ar0521_entity_ops;

	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, 1, &sensor->pad);
	if (ret)
		goto out_media;

	ret = v4l2_ctrl_handler_init(&sensor->ctrls, 10);
	if (ret)
		goto out;

	sensor->subdev.ctrl_handler = &sensor->ctrls;
	sensor->ctrls.lock = &sensor->lock;

	ret = v4l2_async_register_subdev_sensor(&sensor->subdev);
	if (ret)
		goto out;

	return 0;

out:
	v4l2_ctrl_handler_free(&sensor->ctrls);
out_media:
	media_entity_cleanup(&sd->entity);
	mutex_destroy(&sensor->lock);
	return ret;
}

static int ar0521_remove(struct i2c_client *i2c)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(i2c);
	struct ar0521 *sensor = to_ar0521(sd);

	if (!tc358748_stop(i2c))
		pr_err("Can't deinitialize TC358748 through I2C");

#ifdef DEBUG
	ar0521_debugfs_remove(sensor);
#endif /* ifdef DEBUG */
	v4l2_async_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&sensor->ctrls);
	media_entity_cleanup(&sd->entity);
	mutex_destroy(&sensor->lock);

	return 0;
}

static const struct i2c_device_id ar0521_id_table[] = {
	{ "tc358748_driver" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, ar0521_id_table);

static const struct of_device_id ar0521_of_match[] = {
	{ .compatible = "pco,tc358748_driver" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ar0521_of_match);

static struct i2c_driver ar0521_i2c_driver = {
	.driver		= {
		.name	= "tc358748",
		.of_match_table = of_match_ptr(ar0521_of_match),
	},
	.probe		= ar0521_probe,
	.remove		= ar0521_remove,
	.id_table	= ar0521_id_table,
};
module_i2c_driver(ar0521_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("");



/*

	błąd: media-ctl -p
/dev/v4l-subdev1
	pad0: Source
		[fmt:SGRBG12_1X12/2592x1944@10/553 field:none colorspace:raw
		 crop.bounds:(0,0)/2604x1956
		 crop:(4,4)/2592x1944]

new:
		[fmt:SGRBG8_1X8/640x480 field:none colorspace:raw
		 crop.bounds:(0,0)/0x0
		 crop:(0,0)/0x0
		 compose.bounds:(0,0)/0x0
		 compose:(0,0)/0x0]
		-> "mxc-mipi-csi2.0":0 [ENABLED,IMMUTABLE]

*/
