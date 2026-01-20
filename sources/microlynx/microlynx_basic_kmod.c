/*
 *
 * Copyright (c) 2026, Xenics Exosens, All Rights Reserved.
 *
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>

#include "gencp-over-i2c/libunio.h"
#include "gencp-over-i2c/gencp_client.h"

#define PREFIX "I2C test kmodule"
#include "gencp-over-i2c/liblogger.h"

// #define DEFAULT_WIDTH 1024
// #define DEFAULT_HEIGHT 128

#define REG_ACQ_START_W   0x500F0000
#define REG_ACQ_STOP_W    0x500F0004
#define REG_ACQ_STATUS_R  0x500F0008
#define REG_IMG_HEIGHT_RW 0x500E000C
#define REG_IMG_WIDTH_R   0x500E0008
#define REG_MIPI_ENA_R    0x50ff0010
#define REG_FIRW_VER_R    0x50FF0000

/* Mode : resolution and related config&values */
struct sensor_mode {
        u32 width; // Frame width in pixels
        u32 height; // Frame height in pixels
        u32 line_length; // Line length in pixels
        u32 pix_clk_hz; // Pixel clock in Hz
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
   struct unio_handle io_handle;
};

static const struct sensor_mode sensor_supported_modes[] = {
        {
               .width = 1024,
               .height = 128, // NOTE: Value for testing
               .line_length = 1024, // NOTE: Should be no extra padding
               .pix_clk_hz = 60000000, //60MHz
        },
};
#define NUM_SUPPORTED_MODES ARRAY_SIZE(sensor_supported_modes)

u32 sensor_mbus_codes[] = {
   MEDIA_BUS_FMT_Y16_1X16
};
#define NUM_MBUS_CODES ARRAY_SIZE(sensor_mbus_codes)

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
   return EIO;
}

static int microlynx_init_controls(struct sensor_def *sensor) {
   struct v4l2_ctrl_handler *ctrl_hdlr;
   struct v4l2_ctrl *ctrl;
   int ret;

   ctrl_hdlr = &sensor->ctrl_handler;
   ret = v4l2_ctrl_handler_init(ctrl_hdlr, 4);
   if (ret)
      return ret;

   mutex_init(&sensor->mutex);
   ctrl_hdlr->lock = &sensor->mutex;

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

   static const s64 link_freq_menu_items[] = { 240000000 };
   ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr, &sensor_ctrl_ops, V4L2_CID_LINK_FREQ,
                              0, 0, link_freq_menu_items); // NOTE: Unused but have to define it anyways?

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

static int sensor_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
   // Grab the pointer to the main sensor_def from referencing the v4l2_subdev pointer
   struct sensor_def *sensor = container_of(sd, struct sensor_def, sd);
   struct v4l2_mbus_framefmt *try_img_fmt = v4l2_subdev_state_get_format(fh->state, 0);
   struct v4l2_rect *try_crop;

   // Frame format we defined in the probe function
   *try_img_fmt = sensor->fmt;

   /* Initialize try_crop rectangle. */
   try_crop = v4l2_subdev_state_get_crop(fh->state, 0);
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
      struct v4l2_mbus_framefmt *try_fmt =
         v4l2_subdev_state_get_format(sd_state, fmt->pad);
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

static struct sensor_mode * sensor_find_mode(u32 width, u32 height)
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

   struct sensor_mode *mode = sensor_find_mode(fmt->format.width, fmt->format.height);

   if (!mode)
      return -EINVAL;

   fmt->format.width = mode->width;
   // fmt->format.height = mode->height;
   fmt->format.height = sensor->line_height;
   fmt->format.code = sensor_mbus_codes[0];
   fmt->format.field = V4L2_FIELD_NONE;
   fmt->format.colorspace = V4L2_COLORSPACE_RAW;


   if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
      format = v4l2_subdev_state_get_format(sd_state, fmt->pad);
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
   if (code->index >= NUM_MBUS_CODES)
      return -EINVAL;
   if (code->pad)
      return -EINVAL;

   code->code = sensor_mbus_codes[code->index];

   return 0;
}

static int sensor_set_stream(struct v4l2_subdev *sd, int enable)
{
   // Camera should always be streaming. But we can check that.
   int status = 0;
   u32 read_data;
   status = GENCPCLIENT_ReadRegister(REG_ACQ_STATUS_R, &read_data);
   if (status == 0) {
      if (read_data != 0x1){
         PRINT_INFO("Acquisition is off, re-enabling it.");
         status = GENCPCLIENT_WriteRegister(REG_ACQ_START_W, 0x1);
      }
   } else {
      PRINT_INFO("Register read failed\n");
   }

   return 0;
}

// static int sensor_enum_frame_size(struct v4l2_subdev *sd,
//       struct v4l2_subdev_state *sd_state,
//       struct v4l2_subdev_frame_size_enum *fse)
// {
//    uint32_t detectorWidth = DEFAULT_WIDTH;
//    uint32_t detectorHeight = DEFAULT_HEIGHT;
//    struct sensor_def *sensor = container_of(sd, struct sensor_def, sd);
//
//    dev_err(&sensor->i2c_client->dev, "Grabbing frame size enum\n");
//
//    if (fse->index)
//       return -EINVAL;
//    if (fse->pad)
//       return -EINVAL;
//
//    if (fse->index >= NUM_SUPPORTED_MODES)
//       return -EINVAL;
//
//    fse->min_width  = sensor_supported_modes[fse->index].width;
//    fse->max_width  = sensor_supported_modes[fse->index].width;
//    fse->min_height = sensor_supported_modes[fse->index].height;
//    fse->max_height = sensor_supported_modes[fse->index].height;
//
//    // // FIX: Get the actual Width from the sensor here using I2C
//    // // Override detectorWidth here
//    // int err;
//    // err = 0;
//    // if (!err) {
//    //    fse->min_width = detectorWidth;
//    //    fse->max_width = detectorWidth;
//    // } else {
//    //    dev_err(&sensor->i2c_client->dev, "Failed to get detector's width.\n");
//    // }
//    //
//    // // FIX: et the actual Height from the sensor here using I2C
//    // // Override detectorHeight here
//    // err = 0;
//    // if (!err) {
//    //    fse->min_height = detectorHeight;
//    //    fse->max_height = detectorHeight;
//    // } else {
//    //    dev_err(&sensor->i2c_client->dev, "Failed to get detector's height.\n");
//    // }
//
//    return 0;
// }

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
   .enum_mbus_code = sensor_enum_mbus_code,
   .get_fmt = sensor_get_pad_format,
   .set_fmt = sensor_set_pad_format,
   // NOTE: Below is not required for minimal driver
   .get_selection = sensor_get_selection, // This is for ROIs?
   // .enum_frame_size = sensor_enum_frame_size, //For multiple resolutions?
};

static const struct v4l2_subdev_ops sensor_subdev_ops = {
   .core = &sensor_core_ops,
   .video = &sensor_video_ops,
   .pad = &sensor_pad_ops,
};

static const struct v4l2_subdev_internal_ops sensor_internal_ops = {
   .open = sensor_open,
};

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
   sensor->fmt.code = sensor_mbus_codes[0];
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

   ret = v4l2_async_register_subdev_sensor(&sensor->sd);
   if (ret < 0) {
      dev_err(dev, "failed to register sensor sub-device: %d\n", ret);
      goto error_media_entity;
   }

   dev_info(&client->dev, "Minimal CSI sensor driver probed\n");

   dev_info(dev, "registered\n");
   return 0;

   error_media_entity:
      media_entity_cleanup(&sensor->sd.entity);

   error_handler_free:
      sensor_free_controls(sensor);

   return ret;
}

static void microlynx_remove(struct i2c_client *client)
{
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
