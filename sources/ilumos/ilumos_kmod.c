/*
 *
 * Copyright (c) 2026, Exosens, All Rights Reserved.
 *
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,12,0)
#include <asm/unaligned.h>
#else
#include <linux/unaligned.h>
#endif

#define REG_ACQ_START_W   0x500F0000
#define REG_ACQ_STOP_W    0x500F0004
#define REG_ACQ_STATUS_R  0x500F0008
#define REG_IMG_HEIGHT_R  0x500E000C
#define REG_IMG_WIDTH_R   0x500E0008
#define REG_MIPI_ENA_R    0x50ff0010
#define REG_FIRW_VER_R    0x50FF0000
#define REG_PIXEL_FORMAT  0x500e0018
#define PIXEL_FORMAT_MONO16 0x01100007u
#define PIXEL_FORMAT_MONO14 0x01100025u

#define ILUMOS_DEFAULT_WIDTH   1280
#define ILUMOS_DEFAULT_HEIGHT  1024
#define ILUMOS_STR_MAX        256

/* ---- ioctl interface (/dev/ilumos-<bus>-<addr>) ------------------------- */

/**
 * struct ilumos_reg_op - ioctl payload for 32-bit register read/write
 * @addr: register address
 * @val:  value written (WRITE_REG) or value returned (READ_REG)
 */
struct ilumos_reg_op {
	__u32 addr;
	__u32 val;
};

/**
 * struct ilumos_str_op - ioctl payload for string read
 * @addr: register address of the string
 * @len:  number of bytes to read (clamped to ILUMOS_STR_MAX)
 * @buf:  buffer filled with the string data on return
 */
struct ilumos_str_op {
	__u32 addr;
	__u32 len;
	__u8  buf[ILUMOS_STR_MAX];
};

#define ILUMOS_IOCTL_MAGIC    'I'
#define ILUMOS_IOCTL_READ_REG  _IOWR(ILUMOS_IOCTL_MAGIC, 1, struct ilumos_reg_op)
#define ILUMOS_IOCTL_WRITE_REG _IOW (ILUMOS_IOCTL_MAGIC, 2, struct ilumos_reg_op)
#define ILUMOS_IOCTL_READ_STR  _IOWR(ILUMOS_IOCTL_MAGIC, 3, struct ilumos_str_op)

struct sensor_def {
	struct i2c_client *i2c_client;
	struct v4l2_subdev sd;
	struct v4l2_mbus_framefmt fmt;
	struct v4l2_ctrl_handler ctrl_handler;
	struct media_pad pad;
	/*
	 * Mutex for serialized access:
	 * Protect iLumos module set pad format and start/stop streaming safely.
	 */
	struct mutex mutex;
	u32 height;
	u32 width;
	u32 active_mbus_code;

	/* chardev — /dev/ilumos-<bus>-<addr> */
	struct miscdevice miscdev;
	char             miscdev_name[32];
};

struct v4l2_fwnode_endpoint ilumos_ep_cfg = {
	.bus_type = V4L2_MBUS_CSI2_DPHY
};

/* ---- I2C register access ------------------------------------------------ */

static int ilumos_i2c_read_register(struct i2c_client *client, u32 reg, u32 *val)
{
	struct i2c_msg msgs[2];
	u8 tx_data[6];
	u8 rx_data[6]; /* 2 status bytes + 4 data bytes */

	*(u32 *)tx_data = cpu_to_le32(reg);
	*(u16 *)(tx_data + 4) = cpu_to_le16(4);

	msgs[0].addr  = client->addr;
	msgs[0].flags = 0;
	msgs[0].len   = sizeof(tx_data);
	msgs[0].buf   = tx_data;

	msgs[1].addr  = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len   = 6;
	msgs[1].buf   = rx_data;

	if (i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs)) != 2)
		return -EIO;

	if (rx_data[0] != 0 || rx_data[1] != 0)
		return -EINVAL;

	*val = le32_to_cpu(*(u32 *)(rx_data + 2));
	return 0;
}

static int ilumos_i2c_write_register(struct i2c_client *client, u32 reg, u32 val)
{
	struct i2c_msg msgs;
	u8 tx_data[10];

	*(u32 *)tx_data = cpu_to_le32(reg);
	*(u16 *)(tx_data + 4) = cpu_to_le16(4);
	*(u32 *)(tx_data + 6) = cpu_to_le32(val);

	msgs.addr  = client->addr;
	msgs.flags = 0;
	msgs.len   = sizeof(tx_data);
	msgs.buf   = tx_data;

	if (i2c_transfer(client->adapter, &msgs, 1) != 1)
		return -EIO;

	return 0;
}

static int ilumos_i2c_read_string(struct i2c_client *client, u32 reg,
				   u8 *buf, u16 len)
{
	struct i2c_msg msgs[2];
	u8 tx_data[6];
	u8 rx_data[ILUMOS_STR_MAX + 2];

	if (len > ILUMOS_STR_MAX)
		return -EINVAL;

	*(u32 *)tx_data = cpu_to_le32(reg);
	*(u16 *)(tx_data + 4) = cpu_to_le16(len);

	msgs[0].addr  = client->addr;
	msgs[0].flags = 0;
	msgs[0].len   = sizeof(tx_data);
	msgs[0].buf   = tx_data;

	msgs[1].addr  = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len   = len + 2;
	msgs[1].buf   = rx_data;

	if (i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs)) != 2)
		return -EIO;

	if (rx_data[0] != 0 || rx_data[1] != 0)
		return -EINVAL;

	memcpy(buf, rx_data + 2, len);
	return 0;
}

/* ---- chardev file operations ------------------------------------------ */

static int ilumos_cdev_open(struct inode *inode, struct file *file)
{
	struct sensor_def *sensor =
		container_of(file->private_data, struct sensor_def, miscdev);
	file->private_data = sensor;
	return 0;
}

static int ilumos_cdev_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long ilumos_cdev_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	struct sensor_def *sensor = file->private_data;
	int ret = 0;

	switch (cmd) {
	case ILUMOS_IOCTL_READ_REG: {
		struct ilumos_reg_op op;

		if (copy_from_user(&op, (void __user *)arg, sizeof(op))) {
			ret = -EFAULT;
			break;
		}
		ret = ilumos_i2c_read_register(sensor->i2c_client, op.addr, &op.val);
		if (ret == 0 && copy_to_user((void __user *)arg, &op, sizeof(op)))
			ret = -EFAULT;
		break;
	}
	case ILUMOS_IOCTL_WRITE_REG: {
		struct ilumos_reg_op op;

		if (copy_from_user(&op, (void __user *)arg, sizeof(op))) {
			ret = -EFAULT;
			break;
		}
		ret = ilumos_i2c_write_register(sensor->i2c_client, op.addr, op.val);
		break;
	}
	case ILUMOS_IOCTL_READ_STR: {
		struct ilumos_str_op op;

		if (copy_from_user(&op, (void __user *)arg, sizeof(op))) {
			ret = -EFAULT;
			break;
		}
		op.len = min_t(u32, op.len, ILUMOS_STR_MAX);
		ret = ilumos_i2c_read_string(sensor->i2c_client, op.addr,
					     op.buf, op.len);
		if (ret == 0 && copy_to_user((void __user *)arg, &op, sizeof(op)))
			ret = -EFAULT;
		break;
	}
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static const struct file_operations ilumos_cdev_fops = {
	.owner          = THIS_MODULE,
	.open           = ilumos_cdev_open,
	.release        = ilumos_cdev_release,
	.unlocked_ioctl = ilumos_cdev_ioctl,
};

/* ----------------------------------------------------------------------- */

static void sensor_free_controls(struct sensor_def *sensor)
{
	v4l2_ctrl_handler_free(sensor->sd.ctrl_handler);
	mutex_destroy(&sensor->mutex);
}

static int ilumos_check_hwcfg(struct device *dev)
{
	struct fwnode_handle *endpoint;

	dev_info(dev, "Checking hwcfg for existing endpoints");

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	if (v4l2_fwnode_endpoint_alloc_parse(endpoint, &ilumos_ep_cfg)) {
		dev_err(dev, "could not parse endpoint\n");
		goto error_out;
	}

	dev_info(dev, "Completed hwcfg for existing endpoints");
	return 0;

error_out:
	v4l2_fwnode_endpoint_free(&ilumos_ep_cfg);
	fwnode_handle_put(endpoint);
	return -EINVAL;
}

static int sensor_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sensor_def *sensor =
		container_of(ctrl->handler, struct sensor_def, ctrl_handler);
	int ret;

	switch (ctrl->id) {
	default:
		dev_info(&sensor->i2c_client->dev,
			 "ctrl(id:0x%x,val:0x%x) is not handled\n",
			 ctrl->id, ctrl->val);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops sensor_ctrl_ops = {
	.s_ctrl = sensor_set_ctrl,
};

static int ilumos_sensor_check(struct sensor_def *sensor)
{
	int status;
	u32 read_data = 0;

	/* MIPI enable check */
	status = ilumos_i2c_read_register(sensor->i2c_client, REG_MIPI_ENA_R,
					  &read_data);
	if (status == 0) {
		if (read_data == 0x1) {
			dev_info(&sensor->i2c_client->dev,
				 "MIPI is enabled, status = %#08x\n", read_data);
		} else {
			dev_err(&sensor->i2c_client->dev,
				"MIPI is not enabled on this camera, license missing? Exiting...\n");
			goto error_exit;
		}
	} else {
		dev_err(&sensor->i2c_client->dev, "MIPI status read failed\n");
		goto error_exit;
	}

	/* Firmware version */
	status = ilumos_i2c_read_register(sensor->i2c_client, REG_FIRW_VER_R,
					  &read_data);
	if (status == 0) {
		dev_info(&sensor->i2c_client->dev,
			 "FPGA firmware version = %#08x\n", read_data);
	} else {
		dev_err(&sensor->i2c_client->dev, "FPGA firmware read failed\n");
		goto error_exit;
	}

	/* Pixel format detection */
	sensor->active_mbus_code = MEDIA_BUS_FMT_Y16_1X16;
	status = ilumos_i2c_read_register(sensor->i2c_client, REG_PIXEL_FORMAT,
					  &read_data);
	if (status == 0) {
		if (read_data == PIXEL_FORMAT_MONO14) {
			sensor->active_mbus_code = MEDIA_BUS_FMT_Y14_1X14;
			dev_info(&sensor->i2c_client->dev,
				 "PIXEL_FORMAT = 0x%08x (Y14) -> MEDIA_BUS_FMT_Y14_1X14\n",
				 read_data);
		} else {
			dev_info(&sensor->i2c_client->dev,
				 "PIXEL_FORMAT = 0x%08x -> MEDIA_BUS_FMT_Y16_1X16\n",
				 read_data);
		}
	} else {
		dev_info(&sensor->i2c_client->dev,
			 "PIXEL_FORMAT read failed, defaulting to MEDIA_BUS_FMT_Y16_1X16\n");
	}

	/* Resolution */
	sensor->height = ILUMOS_DEFAULT_HEIGHT;
	sensor->width  = ILUMOS_DEFAULT_WIDTH;

	status = ilumos_i2c_read_register(sensor->i2c_client, REG_IMG_HEIGHT_R,
					  &read_data);
	if (status == 0)
		sensor->height = read_data;
	else
		dev_info(&sensor->i2c_client->dev,
			 "Height register read failed, using default %u\n",
			 sensor->height);

	status = ilumos_i2c_read_register(sensor->i2c_client, REG_IMG_WIDTH_R,
					  &read_data);
	if (status == 0)
		sensor->width = read_data;
	else
		dev_info(&sensor->i2c_client->dev,
			 "Width register read failed, using default %u\n",
			 sensor->width);

	return 0;

error_exit:
	dev_err(&sensor->i2c_client->dev, "Probing failed\n");
	return -EIO;
}

static int ilumos_init_controls(struct sensor_def *sensor)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct v4l2_ctrl *ctrl;
	int ret;

	ctrl_hdlr = &sensor->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 5);
	if (ret)
		return ret;

	mutex_init(&sensor->mutex);
	ctrl_hdlr->lock = &sensor->mutex;

	if (ilumos_ep_cfg.nr_of_link_frequencies > 0)
		v4l2_ctrl_new_int_menu(ctrl_hdlr, NULL, V4L2_CID_LINK_FREQ,
				       ilumos_ep_cfg.nr_of_link_frequencies - 1,
				       0, ilumos_ep_cfg.link_frequencies);
	else
		dev_warn(&sensor->i2c_client->dev,
			 "link-frequencies not found in device tree\n");

	v4l2_ctrl_new_std(ctrl_hdlr, &sensor_ctrl_ops, V4L2_CID_PIXEL_RATE,
			  1, 1, 1, 1);

	ctrl = v4l2_ctrl_new_std(ctrl_hdlr, &sensor_ctrl_ops, V4L2_CID_VBLANK,
				 1, 1, 1, 1);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ctrl = v4l2_ctrl_new_std(ctrl_hdlr, &sensor_ctrl_ops, V4L2_CID_HBLANK,
				 1, 1, 1, 1);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ctrl = v4l2_ctrl_new_std(ctrl_hdlr, &sensor_ctrl_ops, V4L2_CID_EXPOSURE,
				 1, 1, 1, 1);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ctrl = v4l2_ctrl_new_std(ctrl_hdlr, &sensor_ctrl_ops,
				 V4L2_CID_ANALOGUE_GAIN, 1, 1, 1, 1);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&sensor->i2c_client->dev,
			"%s control init failed (%d)\n", __func__, ret);
		goto error;
	}

	sensor->sd.ctrl_handler = ctrl_hdlr;
	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&sensor->mutex);
	return ret;
}

static int sensor_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state)
{
	struct sensor_def *sensor = container_of(sd, struct sensor_def, sd);
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_rect *crop;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
	fmt  = v4l2_subdev_get_try_format(sd, state, 0);
	crop = v4l2_subdev_get_try_crop(sd, state, 0);
#else
	fmt  = v4l2_subdev_state_get_format(state, 0);
	crop = v4l2_subdev_state_get_crop(state, 0);
#endif

	*fmt = sensor->fmt;
	crop->top    = 0;
	crop->left   = 0;
	crop->width  = sensor->fmt.width;
	crop->height = sensor->fmt.height;

	return 0;
}

static int sensor_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sensor_def *sensor = container_of(sd, struct sensor_def, sd);
	struct v4l2_mbus_framefmt *try_img_fmt;
	struct v4l2_rect *try_crop;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
	try_img_fmt = v4l2_subdev_get_try_format(sd, fh->state, 0);
#else
	try_img_fmt = v4l2_subdev_state_get_format(fh->state, 0);
#endif

	*try_img_fmt = sensor->fmt;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
	try_crop = v4l2_subdev_get_try_crop(sd, fh->state, 0);
#else
	try_crop = v4l2_subdev_state_get_crop(fh->state, 0);
#endif
	try_crop->top    = 0;
	try_crop->left   = 0;
	try_crop->width  = try_img_fmt->width;
	try_crop->height = try_img_fmt->height;

	return 0;
}

static int sensor_get_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_format *fmt)
{
	struct sensor_def *sensor = container_of(sd, struct sensor_def, sd);

	if (fmt->pad)
		return -EINVAL;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *try_fmt;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
		try_fmt = v4l2_subdev_get_try_format(sd, sd_state, fmt->pad);
#else
		try_fmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
#endif
		fmt->format = *try_fmt;
	} else {
		fmt->format = sensor->fmt;
	}

	return 0;
}

static int sensor_set_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_format *fmt)
{
	struct sensor_def *sensor = container_of(sd, struct sensor_def, sd);
	struct v4l2_mbus_framefmt *format;

	if (fmt->pad)
		return -EINVAL;

	if (fmt->format.width != sensor->width ||
	    fmt->format.height != sensor->height)
		return -EINVAL;

	fmt->format.width      = sensor->width;
	fmt->format.height     = sensor->height;
	fmt->format.code       = sensor->active_mbus_code;
	fmt->format.field      = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_RAW;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
		format = v4l2_subdev_get_try_format(sd, sd_state, fmt->pad);
#else
		format = v4l2_subdev_state_get_format(sd_state, fmt->pad);
#endif
	} else {
		format = &sensor->fmt;
	}

	*format = fmt->format;
	return 0;
}

static int sensor_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct sensor_def *sensor = container_of(sd, struct sensor_def, sd);

	if (code->index > 0)
		return -EINVAL;
	if (code->pad)
		return -EINVAL;

	code->code = sensor->active_mbus_code;
	return 0;
}

static int sensor_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct sensor_def *sensor = container_of(sd, struct sensor_def, sd);
	int status = 0;

	if (enable) {
		/*
		 * Force a stop/start cycle so the camera MIPI transmitter goes
		 * through a proper LP->HS transition.  The DW DPHY on RP1 (RPi5)
		 * requires seeing this transition to synchronise.
		 */
		ilumos_i2c_write_register(sensor->i2c_client, REG_ACQ_STOP_W, 0x1);
		usleep_range(5000, 10000);
		status = ilumos_i2c_write_register(sensor->i2c_client,
						   REG_ACQ_START_W, 0x1);
		if (status)
			dev_err(&sensor->i2c_client->dev,
				"Failed to start acquisition\n");
	} else {
		ilumos_i2c_write_register(sensor->i2c_client, REG_ACQ_STOP_W, 0x1);
	}

	return 0;
}

static int sensor_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct sensor_def *sensor = container_of(sd, struct sensor_def, sd);

	if (fse->index > 0)
		return -EINVAL;
	if (fse->pad)
		return -EINVAL;
	if (fse->code != sensor->active_mbus_code)
		return -EINVAL;

	fse->min_width  = sensor->width;
	fse->max_width  = sensor->width;
	fse->min_height = sensor->height;
	fse->max_height = sensor->height;

	return 0;
}

static int sensor_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *sd_state,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct sensor_def *sensor = container_of(sd, struct sensor_def, sd);

	if (fie->index > 0)
		return -EINVAL;
	if (fie->pad)
		return -EINVAL;
	if (fie->code != sensor->active_mbus_code)
		return -EINVAL;

	fie->interval.numerator   = 1;
	fie->interval.denominator = 120;

	return 0;
}

static int sensor_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_selection *sel)
{
	struct sensor_def *sensor = container_of(sd, struct sensor_def, sd);

	switch (sel->target) {
	case V4L2_SEL_TGT_NATIVE_SIZE:
	default:
		sel->r.top    = 0;
		sel->r.left   = 0;
		sel->r.width  = sensor->fmt.width;
		sel->r.height = sensor->fmt.height;
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_subdev_core_ops sensor_core_ops = {
	.subscribe_event   = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops sensor_video_ops = {
	.s_stream = sensor_set_stream,
};

static const struct v4l2_subdev_pad_ops sensor_pad_ops = {
	.enum_mbus_code      = sensor_enum_mbus_code,
	.get_fmt             = sensor_get_pad_format,
	.set_fmt             = sensor_set_pad_format,
	.get_selection       = sensor_get_selection,
	.enum_frame_size     = sensor_enum_frame_size,
	.enum_frame_interval = sensor_enum_frame_interval,
};

static const struct v4l2_subdev_ops sensor_subdev_ops = {
	.core  = &sensor_core_ops,
	.video = &sensor_video_ops,
	.pad   = &sensor_pad_ops,
};

static const struct v4l2_subdev_internal_ops sensor_internal_ops = {
	.init_state = sensor_init_state,
	.open       = sensor_open,
};

/* ---- Probe / Remove --------------------------------------------------- */

static int ilumos_probe(struct i2c_client *client)
{
	struct sensor_def *sensor;
	struct device *dev = &client->dev;
	int ret;

	dev_info(dev, "Probing sensor: name=%s addr=0x%x adapter=%s\n",
		 client->name, client->addr, dev_name(&client->adapter->dev));

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	i2c_set_clientdata(client, sensor);

	if (ilumos_check_hwcfg(dev))
		return -EINVAL;

	sensor->i2c_client = client;
	v4l2_i2c_subdev_init(&sensor->sd, client, &sensor_subdev_ops);

	ret = ilumos_sensor_check(sensor);
	if (ret)
		return ret;

	sensor->fmt.width      = sensor->width;
	sensor->fmt.height     = sensor->height;
	sensor->fmt.code       = sensor->active_mbus_code;
	sensor->fmt.field      = V4L2_FIELD_NONE;
	sensor->fmt.colorspace = V4L2_COLORSPACE_RAW;

	dev_info(dev, "Capture resolution: %d x %d, color space: %d\n",
		 sensor->fmt.width, sensor->fmt.height, sensor->fmt.colorspace);

	ret = ilumos_init_controls(sensor);
	if (ret)
		return ret;

	sensor->sd.internal_ops = &sensor_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret) {
		dev_err(dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret) {
		dev_err(dev, "failed to init subdev state: %d\n", ret);
		goto error_media_entity;
	}

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret < 0) {
		dev_err(dev, "failed to register sensor sub-device: %d\n", ret);
		goto error_subdev_cleanup;
	}

	/* Register chardev for userspace register access */
	snprintf(sensor->miscdev_name, sizeof(sensor->miscdev_name),
		 "ilumos-%d-%04x", client->adapter->nr, client->addr);
	sensor->miscdev.minor  = MISC_DYNAMIC_MINOR;
	sensor->miscdev.name   = sensor->miscdev_name;
	sensor->miscdev.fops   = &ilumos_cdev_fops;
	sensor->miscdev.parent = dev;
	if (misc_register(&sensor->miscdev))
		dev_warn(dev, "failed to register chardev; ilumosCtrl.py will not work\n");
	else
		dev_info(dev, "chardev registered at /dev/%s\n",
			 sensor->miscdev_name);

	dev_info(dev, "registered\n");
	return 0;

error_subdev_cleanup:
	v4l2_subdev_cleanup(&sensor->sd);

error_media_entity:
	media_entity_cleanup(&sensor->sd.entity);

error_handler_free:
	sensor_free_controls(sensor);

	return ret;
}

static void ilumos_remove(struct i2c_client *client)
{
	struct sensor_def *sensor = i2c_get_clientdata(client);

	misc_deregister(&sensor->miscdev);
	v4l2_async_unregister_subdev(&sensor->sd);
	v4l2_subdev_cleanup(&sensor->sd);
	media_entity_cleanup(&sensor->sd.entity);
	sensor_free_controls(sensor);
	dev_info(&client->dev, "iLumos module removed\n");
}

static const struct of_device_id ilumos_device_id[] = {
	{ .compatible = "exosens,ilumos" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ilumos_device_id);

static struct i2c_driver ilumos_driver = {
	.driver = {
		.name           = "ilumos",
		.of_match_table = ilumos_device_id,
	},
	.probe  = ilumos_probe,
	.remove = ilumos_remove,
};

module_i2c_driver(ilumos_driver);

MODULE_DESCRIPTION("Media Controller driver for Exosens iLumos sensors");
MODULE_AUTHOR("Exosens");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
