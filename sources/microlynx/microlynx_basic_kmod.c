/*
 *
 * Copyright (c) 2026, Xenics Exosens, All Rights Reserved.
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

#include "gencp-over-i2c/libunio.h"
#include "gencp-over-i2c/gencp_client.h"

#define PREFIX "Microlynx"
#include "gencp-over-i2c/liblogger.h"

/* ---- GenCP userspace chardev (/dev/microlynx-<bus>-<addr>) ------------- */

/**
 * struct microlynx_reg_op - ioctl payload for 32-bit register read/write
 * @addr: GenCP register address
 * @val:  value written (WRITE_REG) or value returned (READ_REG)
 */
struct microlynx_reg_op {
	__u32 addr;
	__u32 val;
};

#define MICROLYNX_STR_MAX 256

/**
 * struct microlynx_str_op - ioctl payload for GenCP string read
 * @addr: GenCP register address of the string
 * @len:  number of bytes to read (clamped to MICROLYNX_STR_MAX)
 * @buf:  buffer filled with the string data on return
 */
struct microlynx_str_op {
	__u32 addr;
	__u32 len;
	__u8  buf[MICROLYNX_STR_MAX];
};

#define MICROLYNX_IOCTL_MAGIC    'M'
/* _IOWR('M', 1, struct microlynx_reg_op) */
#define MICROLYNX_IOCTL_READ_REG  _IOWR(MICROLYNX_IOCTL_MAGIC, 1, \
					struct microlynx_reg_op)
/* _IOW('M', 2, struct microlynx_reg_op) */
#define MICROLYNX_IOCTL_WRITE_REG _IOW(MICROLYNX_IOCTL_MAGIC,  2, \
					struct microlynx_reg_op)
/* _IOWR('M', 3, struct microlynx_str_op) */
#define MICROLYNX_IOCTL_READ_STR  _IOWR(MICROLYNX_IOCTL_MAGIC, 3, \
					struct microlynx_str_op)

/*
 * Module-level mutex: GENCPCLIENT uses process-global state (pRxBuffer,
 * pTxBuffer, unio_handle_ptr).  Serialise all GenCP calls behind this lock.
 */
static DEFINE_MUTEX(microlynx_gencp_lock);

// #define DEFAULT_WIDTH 1024
// #define DEFAULT_HEIGHT 128

#define REG_ACQ_START_W   0x500F0000
#define REG_ACQ_STOP_W    0x500F0004
#define REG_ACQ_STATUS_R  0x500F0008
#define REG_IMG_HEIGHT_RW 0x500E000C
#define REG_IMG_WIDTH_R   0x500E0008
#define REG_MIPI_ENA_R    0x50ff0010
#define REG_FIRW_VER_R    0x50FF0000
#define REG_PIXEL_FORMAT  0x500e0018
#define PIXEL_FORMAT_MONO16 0x01100007u
#define PIXEL_FORMAT_MONO14 0x01100025u

/* Mode : resolution and related config&values */
struct sensor_mode {
        u32 width; // Frame width in pixels
        u32 height; // Frame height in pixels
        u32 line_length; // Line length in pixels
};

struct sensor_def {
   struct i2c_client *i2c_client;
   struct v4l2_subdev sd;
   struct v4l2_mbus_framefmt fmt;
   struct v4l2_ctrl_handler ctrl_handler;
   struct media_pad pad;
   /*
    * Mutex for serialized access:
    * Protect eg_ec module set pad format and start/stop streaming safely.
    */
   struct mutex mutex;
   u32 line_height;
   u32 native_width;
   u32 active_mbus_code;
   struct unio_handle io_handle;

   /* GenCP chardev — /dev/microlynx-<bus>-<addr> */
   struct miscdevice    miscdev;
   char                 miscdev_name[32];
};

static const struct sensor_mode sensor_supported_modes[] = {
        {
               .width = 1024,
               .height = 128, // NOTE: Value for testing
               .line_length = 1024, // NOTE: Should be no extra padding
        },
};
#define NUM_SUPPORTED_MODES ARRAY_SIZE(sensor_supported_modes)


struct v4l2_fwnode_endpoint microlynx_ep_cfg = {
   .bus_type = V4L2_MBUS_CSI2_DPHY
};

static void sensor_free_controls(struct sensor_def *sensor)
{
   printk("DEBUG: sensor free controls");
   v4l2_ctrl_handler_free(sensor->sd.ctrl_handler);
   mutex_destroy(&sensor->mutex);
}

static int microlynx_check_hwcfg(struct device *dev) {
   struct fwnode_handle *endpoint;

   dev_info(dev, "Checking hwcfg for existing endpoints");

   endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
   if (!endpoint) {
      dev_err(dev, "endpoint node not found\n");
      return -EINVAL;
   }

   if (v4l2_fwnode_endpoint_alloc_parse(endpoint, &microlynx_ep_cfg)) {
      dev_err(dev, "could not parse endpoint\n");
      goto error_out;
   }

   dev_info(dev, "Completed hwcfg for existing endpoints");
   return 0;

   error_out:
      v4l2_fwnode_endpoint_free(&microlynx_ep_cfg);
      fwnode_handle_put(endpoint);
      return -EINVAL;
}

static int sensor_set_ctrl(struct v4l2_ctrl *ctrl)
{
   printk("Sensor_set_ctrl called");
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

static int microlynx_sensor_check(struct sensor_def *sensor) {
   sensor->io_handle.client = sensor->i2c_client;

   // INIT the gencp client
   GENCPCLIENT_Init(&sensor->io_handle);

   int status;
   u32 read_data = 0;

   //FPGA test read
   // mipi enable register to read 50ff0010
   status = GENCPCLIENT_ReadRegister(REG_MIPI_ENA_R, &read_data);
   if (status == 0) {
      if (read_data == 0x1){
         PRINT_INFO("MIPI is enabled, status = %#08x\n", read_data);
      } else {
         PRINT_ERROR("MIPI is not enabled on this camera, license missing? Exiting...\n");
         goto error_exit;
      }
   } else {
      PRINT_INFO("MIPI status read failed\n");
      goto error_exit;
   }

   //FPGA firmware read
   status = GENCPCLIENT_ReadRegister(REG_FIRW_VER_R, &read_data);
   if (status == 0) {
      PRINT_INFO("FPGA firmware version = %#08x\n", read_data);
   } else {
      PRINT_INFO("FPGA firmware read failed\n");
      goto error_exit;
   }

   // Pixel format detection
   sensor->active_mbus_code = MEDIA_BUS_FMT_Y16_1X16;
   status = GENCPCLIENT_ReadRegister(REG_PIXEL_FORMAT, &read_data);
   if (status == 0) {
      if (read_data == PIXEL_FORMAT_MONO14) {
         sensor->active_mbus_code = MEDIA_BUS_FMT_Y14_1X14;
         PRINT_INFO("PIXEL_FORMAT = 0x%08x (Y14) -> MEDIA_BUS_FMT_Y14_1X14\n", read_data);
      } else {
         PRINT_INFO("PIXEL_FORMAT = 0x%08x -> MEDIA_BUS_FMT_Y16_1X16\n", read_data);
      }
   } else {
      PRINT_INFO("PIXEL_FORMAT read failed, defaulting to MEDIA_BUS_FMT_Y16_1X16\n");
   }

   // Resolution
   // Set Height
   device_property_read_u32(&sensor->i2c_client->dev, "line-height", &sensor->line_height);
   printk("line height read: %u.\n", sensor->line_height);

   // Height check
   status = GENCPCLIENT_ReadRegister(REG_IMG_HEIGHT_RW, &read_data);
   if (status == 0) {
      if (read_data == sensor->line_height){
         PRINT_INFO("Camera and driver line heights match, height = %#08x\n", read_data);
      } else {
         // NOTE: Should driver program the camera if there is a missmatch?
         PRINT_ERROR("Camera and drier line heights don't match. Make sure the correct line height is set on the device tree overlay or on the camera.\n");
         PRINT_ERROR("Camera = %u, Driver = %u\n", read_data, sensor->line_height);
      }
   } else {
      PRINT_INFO("Register read failed\n");
      goto error_exit;
   }

   // Set camera width
   sensor->native_width = 1024;
   status = GENCPCLIENT_ReadRegister(REG_IMG_WIDTH_R, &read_data);
   if (status == 0) {
      sensor->native_width = read_data;
   } else {
      PRINT_INFO("Register read failed\n");
      goto error_exit;
   }

   return 0;

error_exit:
   dev_err(&sensor->i2c_client->dev, "Probing failed");
   GENCPCLIENT_Cleanup();
   return -EIO;
}

static int microlynx_init_controls(struct sensor_def *sensor) {
   struct v4l2_ctrl_handler *ctrl_hdlr;
   struct v4l2_ctrl *ctrl;
   int ret;

   ctrl_hdlr = &sensor->ctrl_handler;
   ret = v4l2_ctrl_handler_init(ctrl_hdlr, 5);
   if (ret)
      return ret;

   mutex_init(&sensor->mutex);
   ctrl_hdlr->lock = &sensor->mutex;

   if (microlynx_ep_cfg.nr_of_link_frequencies > 0)
      v4l2_ctrl_new_int_menu(ctrl_hdlr, NULL, V4L2_CID_LINK_FREQ,
            microlynx_ep_cfg.nr_of_link_frequencies - 1, 0,
            microlynx_ep_cfg.link_frequencies);
   else
      dev_warn(&sensor->i2c_client->dev,
               "link-frequencies not found in device tree\n");

   // NOTE: Maybe add the v4l2 resoluion/pixel format controls here?
   // But it's not really compliant and expects userspace programs to set it
   // rather than the subdev driver through the kernel

   /* By default, PIXEL_RATE is read only */
   v4l2_ctrl_new_std(ctrl_hdlr, &sensor_ctrl_ops, V4L2_CID_PIXEL_RATE,
         1, 1, 1, 1);

   /* Initial vblank/hblank/exposure parameters based on current mode */
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

   ctrl = v4l2_ctrl_new_std(ctrl_hdlr, &sensor_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
         1, 1, 1, 1);
   if (ctrl)
      ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

   if (ctrl)
      ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;


   if (ctrl_hdlr->error) {
      ret = ctrl_hdlr->error;
      dev_err(&sensor->i2c_client->dev, "%s control init failed (%d)\n",
            __func__, ret);
      goto error;
   }

   sensor->sd.ctrl_handler = ctrl_hdlr;

   return 0;

   error:
      v4l2_ctrl_handler_free(ctrl_hdlr);
      mutex_destroy(&sensor->mutex);

   return ret;
};

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
   // Grab the pointer to the main sensor_def from referencing the v4l2_subdev pointer
   struct sensor_def *sensor = container_of(sd, struct sensor_def, sd);
   struct v4l2_mbus_framefmt *try_img_fmt;
   struct v4l2_rect *try_crop;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
   try_img_fmt = v4l2_subdev_get_try_format(sd, fh->state, 0);
#else
   try_img_fmt = v4l2_subdev_state_get_format(fh->state, 0);
#endif

   // Frame format we defined in the probe function
   *try_img_fmt = sensor->fmt;

   /* Initialize try_crop rectangle. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
   try_crop = v4l2_subdev_get_try_crop(sd, fh->state, 0);
#else
   try_crop = v4l2_subdev_state_get_crop(fh->state, 0);
#endif
   try_crop->top = 0;
   try_crop->left = 0;
   try_crop->width = try_img_fmt->width;
   try_crop->height = try_img_fmt->height;

   return 0;
}

// TODO: Expand this function similar to the eg_ec driver so we can get current sensor format values
static int sensor_get_pad_format(struct v4l2_subdev *sd,
      struct v4l2_subdev_state *sd_state,
      struct v4l2_subdev_format *fmt)
{
   struct sensor_def *sensor = container_of(sd, struct sensor_def, sd);

   // printk("DEBUG: GET sensor pad format called\n");
   if (fmt->pad)
      return -EINVAL;

   // This tries to configure the setting without actually setting the camera?
   if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
      struct v4l2_mbus_framefmt *try_fmt;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
      try_fmt = v4l2_subdev_get_try_format(sd, sd_state, fmt->pad);
#else
      try_fmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
#endif
      fmt->format = *try_fmt;
      // printk("DEBUG: Try: %d x %d, color: %d\n", fmt->format.width, fmt->format.height, fmt->format.colorspace);
   }
   else
   {
      // TODO: PAD settings would be written here
      fmt->format = sensor->fmt;
      // printk("DEBUG: Else: %d x %d, color: %d\n", fmt->format.width, fmt->format.height, fmt->format.colorspace);
   }

   return 0;
}

static const struct sensor_mode *sensor_find_mode(u32 width, u32 height)
{
    int i;
    for (i = 0; i < NUM_SUPPORTED_MODES; i++) {
        if (sensor_supported_modes[i].width == width &&
            sensor_supported_modes[i].height == height)
            return &sensor_supported_modes[i];
    }
    return NULL;
}


static int sensor_set_pad_format(
      struct v4l2_subdev *sd,
      struct v4l2_subdev_state *sd_state,
      struct v4l2_subdev_format *fmt)
{
   struct sensor_def *sensor = container_of(sd, struct sensor_def, sd);
   struct v4l2_mbus_framefmt *format;

   printk("DEBUG: SET sensor pad format called");
   if (fmt->pad)
      return -EINVAL;

   const struct sensor_mode *mode = sensor_find_mode(fmt->format.width, fmt->format.height);

   if (!mode)
      return -EINVAL;

   fmt->format.width = mode->width;
   // fmt->format.height = mode->height;
   fmt->format.height = sensor->line_height;
   fmt->format.code = sensor->active_mbus_code;
   fmt->format.field = V4L2_FIELD_NONE;
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
   int status = 0;

   mutex_lock(&microlynx_gencp_lock);
   if (enable) {
      /*
       * Force a stop/start cycle so the camera MIPI transmitter goes
       * through a proper LP->HS transition.  The DW DPHY on RP1 (RPi5)
       * requires seeing this transition to synchronise; without it the
       * DPHY never locks and no frames are captured.
       */
      GENCPCLIENT_WriteRegister(REG_ACQ_STOP_W, 0x1);
      usleep_range(5000, 10000);
      status = GENCPCLIENT_WriteRegister(REG_ACQ_START_W, 0x1);
      if (status)
         PRINT_ERROR("Failed to start acquisition\n");
//      else
//         PRINT_INFO("Acquisition started\n");
   } else {
      GENCPCLIENT_WriteRegister(REG_ACQ_STOP_W, 0x1);
//     PRINT_INFO("Acquisition stopped\n");
   }
   mutex_unlock(&microlynx_gencp_lock);

   return 0;
}

static int sensor_enum_frame_size(struct v4l2_subdev *sd,
      struct v4l2_subdev_state *sd_state,
      struct v4l2_subdev_frame_size_enum *fse)
{
   struct sensor_def *sensor = container_of(sd, struct sensor_def, sd);

   if (fse->index >= NUM_SUPPORTED_MODES)
      return -EINVAL;
   if (fse->pad)
      return -EINVAL;
   if (fse->code != sensor->active_mbus_code)
      return -EINVAL;

   fse->min_width  = sensor->native_width;
   fse->max_width  = sensor->native_width;
   fse->min_height = sensor->line_height;
   fse->max_height = sensor->line_height;

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
         sel->r.top = 0;
         sel->r.left = 0;
         sel->r.width = sensor->fmt.width;
         sel->r.height = sensor->fmt.height;
         return 0;

      default:
         // NOTE: These are required by default? Just return a box
         // V4L2_SEL_TGT_CROP
         // V4L2_SEL_TGT_CROP_DEFAULT
         // V4L2_SEL_TGT_CROP_BOUNDS
         sel->r.top = 0;
         sel->r.left = 0;
         sel->r.width = sensor->fmt.width;
         sel->r.height = sensor->fmt.height;
         // return -EINVAL; // FIX: Verify if we should return this
         return 0;
   }

   return -EINVAL;
}

static const struct v4l2_subdev_core_ops sensor_core_ops = {
   .subscribe_event = v4l2_ctrl_subdev_subscribe_event,
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
   .core = &sensor_core_ops,
   .video = &sensor_video_ops,
   .pad = &sensor_pad_ops,
};

static const struct v4l2_subdev_internal_ops sensor_internal_ops = {
   .init_state = sensor_init_state,
   .open       = sensor_open,
};

/* ---- GenCP chardev file operations ------------------------------------ */

static int microlynx_cdev_open(struct inode *inode, struct file *file)
{
	/*
	 * The misc layer sets file->private_data to the struct miscdevice *.
	 * Use container_of to reach the enclosing struct sensor_def.
	 */
	struct sensor_def *sensor =
		container_of(file->private_data, struct sensor_def, miscdev);
	file->private_data = sensor;
	return 0;
}

static int microlynx_cdev_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long microlynx_cdev_ioctl(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	struct sensor_def *sensor __maybe_unused = file->private_data;
	int ret = 0;

	mutex_lock(&microlynx_gencp_lock);

	switch (cmd) {
	case MICROLYNX_IOCTL_READ_REG: {
		struct microlynx_reg_op op;

		if (copy_from_user(&op, (void __user *)arg, sizeof(op))) {
			ret = -EFAULT;
			break;
		}
		ret = GENCPCLIENT_ReadRegister(op.addr, &op.val);
		if (ret == 0 && copy_to_user((void __user *)arg, &op, sizeof(op)))
			ret = -EFAULT;
		break;
	}
	case MICROLYNX_IOCTL_WRITE_REG: {
		struct microlynx_reg_op op;

		if (copy_from_user(&op, (void __user *)arg, sizeof(op))) {
			ret = -EFAULT;
			break;
		}
		ret = GENCPCLIENT_WriteRegister(op.addr, op.val);
		break;
	}
	case MICROLYNX_IOCTL_READ_STR: {
		struct microlynx_str_op op;

		if (copy_from_user(&op, (void __user *)arg, sizeof(op))) {
			ret = -EFAULT;
			break;
		}
		op.len = min_t(u32, op.len, MICROLYNX_STR_MAX);
		ret = GENCPCLIENT_ReadString(op.addr, op.buf, op.len);
		if (ret == 0 && copy_to_user((void __user *)arg, &op, sizeof(op)))
			ret = -EFAULT;
		break;
	}
	default:
		ret = -ENOTTY;
		break;
	}

	mutex_unlock(&microlynx_gencp_lock);
	return ret;
}

static const struct file_operations microlynx_cdev_fops = {
	.owner          = THIS_MODULE,
	.open           = microlynx_cdev_open,
	.release        = microlynx_cdev_release,
	.unlocked_ioctl = microlynx_cdev_ioctl,
};

/* ----------------------------------------------------------------------- */

// Main probe function to detect hardware
static int microlynx_probe(struct i2c_client *client)
{
   struct sensor_def *sensor;
   struct device *dev = &client->dev;
   int ret;

   dev_info(&client->dev,
            "Probing sensor: name=%s addr=0x%x adapter=%s\n",
            client->name, client->addr, dev_name(&client->adapter->dev));

   sensor = devm_kzalloc(&client->dev, sizeof(*sensor), GFP_KERNEL);
   if (!sensor)
      return -ENOMEM;

   i2c_set_clientdata(client, sensor);

   /* Check the hardware configuration in device tree */
   if (microlynx_check_hwcfg(dev))
      return -EINVAL;

   sensor->i2c_client = client;
   v4l2_i2c_subdev_init(&sensor->sd, client, &sensor_subdev_ops);

   ret = microlynx_sensor_check(sensor);
   if (ret)
      return ret;

   //Define the initial camera format
   // sensor->fmt.width = sensor_supported_modes[0].width;
   // sensor->fmt.height = sensor_supported_modes[0].height;
   sensor->fmt.width = sensor->native_width;
   sensor->fmt.height = sensor->line_height;
   sensor->fmt.code = sensor->active_mbus_code;
   sensor->fmt.field = V4L2_FIELD_NONE;
   sensor->fmt.colorspace = V4L2_COLORSPACE_RAW;


   dev_info(&client->dev, "Capture resolution: %d x %d, color space: %d\n", sensor->fmt.width, sensor->fmt.height, sensor->fmt.colorspace);

   /* Setup control handler */
   ret = microlynx_init_controls(sensor);
   if (ret)
      return ret;


   /* Initialize subdev */ // TODO: Verify this?
   sensor->sd.internal_ops = &sensor_internal_ops;
   // FIX : verify flags
   sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
      V4L2_SUBDEV_FL_HAS_EVENTS;
   sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR; // setup for cameras

   /* Initialize source pads */
   sensor->pad.flags = MEDIA_PAD_FL_SOURCE;

   ret = media_entity_pads_init(&sensor->sd.entity, 1,
         &sensor->pad);
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

   /* Register GenCP chardev for userspace access */
   snprintf(sensor->miscdev_name, sizeof(sensor->miscdev_name),
         "microlynx-%d-%04x", client->adapter->nr, client->addr);
   sensor->miscdev.minor  = MISC_DYNAMIC_MINOR;
   sensor->miscdev.name   = sensor->miscdev_name;
   sensor->miscdev.fops   = &microlynx_cdev_fops;
   sensor->miscdev.parent = dev;
   if (misc_register(&sensor->miscdev))
      dev_warn(dev, "failed to register GenCP chardev; userspace access via microlynxCtrl.py will not work\n");
   else
      dev_info(dev, "GenCP chardev registered at /dev/%s\n",
            sensor->miscdev_name);

   dev_info(&client->dev, "Minimal CSI sensor driver probed\n");

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

static void microlynx_remove(struct i2c_client *client)
{
    struct sensor_def *sensor = i2c_get_clientdata(client);

    misc_deregister(&sensor->miscdev);
    v4l2_async_unregister_subdev(&sensor->sd);
    v4l2_subdev_cleanup(&sensor->sd);
    media_entity_cleanup(&sensor->sd.entity);
    sensor_free_controls(sensor);
    GENCPCLIENT_Cleanup();
    dev_info(&client->dev, "Microlynx module removed\n");
}

// This is defined in the device tree
static const struct of_device_id microlynx_device_id[] = {
   { .compatible = "xenics,microlynx" },
   { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, microlynx_device_id);

static struct i2c_driver microlynx_driver = {
   .driver = {
      .name = "microlynx",
      .of_match_table = microlynx_device_id,
   },
   .probe = microlynx_probe,
   .remove = microlynx_remove,
};

module_i2c_driver(microlynx_driver);

MODULE_DESCRIPTION("Media Controller driver for Xenics Microlynx IR sensors");
MODULE_AUTHOR("Xenics Exosens");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
