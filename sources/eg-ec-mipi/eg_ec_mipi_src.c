// SPDX-License-Identifier: GPL-2.0
/*
 * A V4L2 driver for Xenics Exosens MIPI EngineCore cameras.
 *
 * Based on dummy eg_ec driver
 * Copyright (C) 2023 Xenics Exosens
 *
 */

#include <linux/version.h>

#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <asm/unaligned.h>

#include "ecctrl_i2c_common.h"

#define MAX_I2C_CLIENTS_NUMBER 128

#define EC_FEATURE_PREDIFINED_FORMAT   0x260
#define EC_FEATURE_DETECTOR_WIDTH      0x180
#define EC_FEATURE_DETECTOR_HEIGHT     0x184

#define EC_PREDEFINED_FORMAT_Y16   20
#define EC_PREDEFINED_FORMAT_RGB   21
#define EC_PREDEFINED_FORMAT_YCBCR 22


#define DEFAULT_WIDTH	640
#define DEFAULT_HEIGHT	480
/* Default format will be the first entry in eg_ec_mbus_codes */

/* Array of all the mbus formats that we'll accept */
u32 eg_ec_mbus_codes[] = {
   MEDIA_BUS_FMT_UYVY8_1X16,
   MEDIA_BUS_FMT_RGB888_1X24,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,1,63)
   MEDIA_BUS_FMT_Y16_1X16,
#endif
};
#define NUM_MBUS_CODES ARRAY_SIZE(eg_ec_mbus_codes)

/* Mode : resolution and related config&values */
struct eg_ec_mode {
        u32 width; // Frame width in pixels
        u32 height; // Frame height in pixels
};

static const struct eg_ec_mode eg_ec_supported_modes[] = {
        {
               .width = 640,
               .height = 480,
        },
        {
               .width = 1280,
               .height = 1024,
        },
};

static const s64 link_freq_menu_items[] = { 240000000 };

struct eg_ec_i2c_client {
   struct i2c_client *i2c_client;
   struct i2c_adapter *root_adap;
   char chnod_name[128];
   int i2c_locked ;
   int chnod_major_number;
   dev_t chnod_device_number;
   struct class *pClass_chnod;
};

struct eg_ec_i2c_client i2c_clients[MAX_I2C_CLIENTS_NUMBER];

struct eg_ec {
   struct i2c_client *i2c_client;
   struct v4l2_subdev sd;
   struct media_pad pad;

   struct v4l2_mbus_framefmt fmt;

   struct v4l2_ctrl_handler ctrl_handler;
   /*
    * Mutex for serialized access:
    * Protect eg_ec module set pad format and start/stop streaming safely.
    */
   struct mutex mutex;
   int mbus_code_index;
};

int eg_ec_chnod_open (struct inode * pInode, struct file * file);
int eg_ec_chnod_release (struct inode * pInode, struct file * file);

static inline struct eg_ec *to_eg_ec(struct v4l2_subdev *_sd)
{
   return container_of(_sd, struct eg_ec, sd);
}

static ssize_t eg_ec_chnod_read(
      struct file *file_ptr
      , char __user *user_buffer
      , size_t count
      , loff_t *position)
{
   int ret = -EINVAL;
   int i;
   u8 *buffer_i2c = NULL;

   // printk( KERN_NOTICE "dal chnod: Device file read at offset = %i, bytes count = %u\n"
   // , (int)*position
   // , (unsigned int)count );

   for (i = 0; i < MAX_I2C_CLIENTS_NUMBER; i++)
   {
      if (strcmp(i2c_clients[i].chnod_name, file_ptr->f_path.dentry->d_name.name) == 0)
      {
         buffer_i2c =  kmalloc(count, GFP_KERNEL);
         if (buffer_i2c)
         {
 
            ret = i2c_master_recv(i2c_clients[i].i2c_client, buffer_i2c, count);
            if (ret <= 0)
            {
               printk(KERN_ERR "%s : Error sending read request, ret = %d\n", __func__, ret);
               kfree(buffer_i2c);
               return -1;
            }

            if( copy_to_user(user_buffer, buffer_i2c, count) != 0 )
            {
               printk(KERN_ERR "%s : Error, failed to copy from user\n", __func__);
               kfree(buffer_i2c);
               return -EFAULT;
            }

            kfree(buffer_i2c);
         }
         else
         {
            printk(KERN_ERR "%s : Error allocating memory\n", __func__);
            return -1;
         }
      }
   }

   return ret;
}

static ssize_t eg_ec_chnod_write(
      struct file *file_ptr
      , const char __user *user_buffer
      , size_t count
      , loff_t *position)
{
   int ret = -EINVAL;
   u8 *buffer_i2c = NULL;
   int i;

   for (i = 0; i < MAX_I2C_CLIENTS_NUMBER; i++)
   {
      if (strcmp(i2c_clients[i].chnod_name, file_ptr->f_path.dentry->d_name.name) == 0)
      {
         // printk( KERN_NOTICE "chnod: Device file write at offset = %i, bytes count = %u\n"
         // , (int)*position
         // , (unsigned int)count );

         buffer_i2c =  kmalloc(count, GFP_KERNEL);
         if (buffer_i2c)
         {
            if( copy_from_user(buffer_i2c, user_buffer, count) != 0 )
            {
               printk(KERN_ERR "%s : Error, failed to copy from user\n", __func__);
               kfree(buffer_i2c);
               return -EFAULT;
            }

            ret = i2c_master_send(i2c_clients[i].i2c_client, buffer_i2c, count);
            if (ret <= 0)
            {
               printk(KERN_ERR "%s : Error sending Write request, ret = %d\n", __func__, ret);
               kfree(buffer_i2c);
               return -1;
            }
            kfree(buffer_i2c);
         }
         else
         {
            printk(KERN_ERR "%s : Error allocating memory\n", __func__);
            return -1;
         }
         break;
      }
   }

   return ret;
}

int eg_ec_chnod_open (struct inode * pInode, struct file * file)
{
   int i;
   for (i = 0; i < MAX_I2C_CLIENTS_NUMBER; i++)
   {
      if (strcmp(i2c_clients[i].chnod_name, file->f_path.dentry->d_name.name) == 0)
      {
         if (i2c_clients[i].i2c_locked == 0)
         {
            i2c_clients[i].i2c_locked = 1;
            return 0;
         }
         else
         {
            return -EBUSY;
         }
         break;
      }
   }
   return -EINVAL;
}

int eg_ec_chnod_release (struct inode * pInode, struct file * file)
{
   int i;
   for (i = 0; i < MAX_I2C_CLIENTS_NUMBER; i++)
   {
      if (strcmp(i2c_clients[i].chnod_name, file->f_path.dentry->d_name.name) == 0)
      {
         i2c_clients[i].i2c_locked = 0;
         return 0;
      }
   }
   return -EINVAL;
}

static long eg_ec_chnod_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
   int i;
   switch (cmd) {
      case ECCTRL_I2C_TIMEOUT_SET:
         {
            for (i = 0; i < MAX_I2C_CLIENTS_NUMBER; i++)
            {
               if (strcmp(i2c_clients[i].chnod_name, file->f_path.dentry->d_name.name) == 0)
               {
                  i2c_clients[i].root_adap->timeout = msecs_to_jiffies((int)arg);
                  return 0;
               }
            }
            return -EINVAL;
         }
      default:
         {
            return -EINVAL;
         }
   }
}


static struct file_operations eg_ec_chnod_register_fops = 
{
   .owner   = THIS_MODULE,
   .read    = eg_ec_chnod_read,
   .write   = eg_ec_chnod_write,
   .open    = eg_ec_chnod_open,
   .release = eg_ec_chnod_release,
   .unlocked_ioctl = eg_ec_chnod_ioctl,
};
static inline int eg_ec_chnod_register_device(int i2c_ind)
{
   struct device *pDev;
   int result = 0;
   result = register_chrdev( 0, i2c_clients[i2c_ind].chnod_name, &eg_ec_chnod_register_fops );
   if( result < 0 )
   {
      printk( KERN_WARNING "register chnod:  can\'t register character device with error code = %i\n", result );
      return result;
   }
   i2c_clients[i2c_ind].chnod_major_number = result;
   printk( KERN_DEBUG "register chnod: registered character device with major number = %i and minor numbers 0...255\n", i2c_clients[i2c_ind].chnod_major_number );

   i2c_clients[i2c_ind].chnod_device_number = MKDEV(i2c_clients[i2c_ind].chnod_major_number, 0);

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,4,0)
   i2c_clients[i2c_ind].pClass_chnod = class_create(THIS_MODULE, i2c_clients[i2c_ind].chnod_name);
#else
   i2c_clients[i2c_ind].pClass_chnod = class_create(i2c_clients[i2c_ind].chnod_name);
#endif
   if (IS_ERR(i2c_clients[i2c_ind].pClass_chnod)) {
      printk(KERN_WARNING "\ncan't create class");
      unregister_chrdev_region(i2c_clients[i2c_ind].chnod_device_number, 1);
      return -EIO;
   }

   if (IS_ERR(pDev = device_create(i2c_clients[i2c_ind].pClass_chnod, NULL, i2c_clients[i2c_ind].chnod_device_number, NULL, i2c_clients[i2c_ind].chnod_name))) {
      printk(KERN_WARNING "Can't create device /dev/%s\n", i2c_clients[i2c_ind].chnod_name);
      class_destroy(i2c_clients[i2c_ind].pClass_chnod);
      unregister_chrdev_region(i2c_clients[i2c_ind].chnod_device_number, 1);
      return -EIO;
   }
   return 0;
}


static inline int eg_ec_mipi_write_reg(struct i2c_client * i2c_client, uint16_t address, uint8_t *data, int size)
{
   ecctrl_i2c_t args;
   int i;
   int err;
   for (i = 0; i < MAX_I2C_CLIENTS_NUMBER; i++)
   {
      if (i2c_clients[i].i2c_client == i2c_client)
      {
         if (i2c_clients[i].i2c_locked == 0)
         {
            i2c_clients[i].i2c_locked = 1;
            __ecctrl_i2c_timeout_set(i2c_client, 100);
            args.data_address = address;
            args.data = data;
            args.data_size = size;
            args.i2c_timeout = 0;
            args.i2c_tries_max = -1;
            args.cb = NULL;
            err = __ecctrl_i2c_write_reg(i2c_client, &args);
            i2c_clients[i].i2c_locked = 0;
            return err;
         }
         else
         {
            return -EBUSY;
         }
      }
   }
   return -EINVAL;
}

static inline int eg_ec_mipi_read_reg(struct i2c_client * i2c_client, uint16_t address, uint8_t *data, uint8_t size)
{
   ecctrl_i2c_t args;
   int i;
   int err;
   for (i = 0; i < MAX_I2C_CLIENTS_NUMBER; i++)
   {
      if (i2c_clients[i].i2c_client == i2c_client)
      {
         if (i2c_clients[i].i2c_locked == 0)
         {
            i2c_clients[i].i2c_locked = 1;
            __ecctrl_i2c_timeout_set(i2c_client, 100);
            args.data_address = address;
            args.data = data;
            args.data_size = size;
            args.i2c_timeout = 1000;
            args.i2c_tries_max = 1;
            args.cb = NULL;
            err = __ecctrl_i2c_read_reg(i2c_client, &args);
            i2c_clients[i].i2c_locked = 0;
            return err;
         }
         else
         {
            return -EBUSY;
         }
      }
   }
   return -EINVAL;
}

static inline int eg_ec_mipi_write_fifo(struct i2c_client * i2c_client, uint16_t address, uint8_t *data, uint32_t size)
{
   ecctrl_i2c_t args;
   int i;
   int err;
   for (i = 0; i < MAX_I2C_CLIENTS_NUMBER; i++)
   {
      if (i2c_clients[i].i2c_client == i2c_client)
      {
         if (i2c_clients[i].i2c_locked == 0)
         {
            i2c_clients[i].i2c_locked = 1;
            __ecctrl_i2c_timeout_set(i2c_client, 100);
            args.data_address = address;
            args.data = data;
            args.data_size = size;
            args.i2c_timeout = 0;
            args.i2c_tries_max = 0;
            args.cb = NULL;
            args.fifo_flags = FIFO_FLAG_START | FIFO_FLAG_END;
            err = __ecctrl_i2c_write_fifo(i2c_client, &args);
            i2c_clients[i].i2c_locked = 0;
            return err;
         }
         else
         {
            return -EBUSY;
         }
      }
   }
   return -EINVAL;

}

static inline int eg_ec_mipi_read_fifo(struct i2c_client * i2c_client, uint16_t address, uint8_t *data, uint32_t size)
{
   ecctrl_i2c_t args;
   int i;
   int err;
   for (i = 0; i < MAX_I2C_CLIENTS_NUMBER; i++)
   {
      if (i2c_clients[i].i2c_client == i2c_client)
      {
         if (i2c_clients[i].i2c_locked == 0)
         {
            i2c_clients[i].i2c_locked = 1;
            __ecctrl_i2c_timeout_set(i2c_client, 100);
            args.data_address = address;
            args.data = data;
            args.data_size = size;
            args.i2c_timeout = 0;
            args.i2c_tries_max = -1;
            args.cb = NULL;
            args.fifo_flags = FIFO_FLAG_START | FIFO_FLAG_END;
            err = __ecctrl_i2c_read_fifo(i2c_client, &args);
            i2c_clients[i].i2c_locked = 0;
            return err;
         }
         else
         {
            return -EBUSY;
         }
      }
   }
   return -EINVAL;
}

static int eg_ec_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
   struct eg_ec *eg_ec = to_eg_ec(sd);
   struct v4l2_mbus_framefmt *try_img_fmt =
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
      v4l2_subdev_get_try_format(sd, fh->state, 0);
#else
      v4l2_subdev_state_get_format(fh->state, 0);
#endif
   struct v4l2_rect *try_crop;

   *try_img_fmt = eg_ec->fmt;

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

static int eg_ec_set_ctrl(struct v4l2_ctrl *ctrl)
{
   struct eg_ec *eg_ec =
      container_of(ctrl->handler, struct eg_ec, ctrl_handler);
   int ret;

   switch (ctrl->id) {
      default:
         dev_info(&eg_ec->i2c_client->dev,
               "ctrl(id:0x%x,val:0x%x) is not handled\n",
               ctrl->id, ctrl->val);
         ret = -EINVAL;
         break;
   }

   return ret;
}

static const struct v4l2_ctrl_ops eg_ec_ctrl_ops = {
   .s_ctrl = eg_ec_set_ctrl,
};

static int eg_ec_enum_mbus_code(struct v4l2_subdev *sd,
      struct v4l2_subdev_state *sd_state,
      struct v4l2_subdev_mbus_code_enum *code)
{
   if (code->index >= NUM_MBUS_CODES)
      return -EINVAL;
   if (code->pad)
      return -EINVAL;

   code->code = eg_ec_mbus_codes[code->index];

   return 0;
}

static int eg_ec_enum_frame_size(struct v4l2_subdev *sd,
      struct v4l2_subdev_state *sd_state,
      struct v4l2_subdev_frame_size_enum *fse)
{
   int err;
   uint32_t detectorWidth = DEFAULT_WIDTH;
   uint32_t detectorHeight = DEFAULT_HEIGHT;
   struct eg_ec *eg_ec = to_eg_ec(sd);
   if (fse->index)
      return -EINVAL;
   if (fse->pad)
      return -EINVAL;

   if (fse->index >= ARRAY_SIZE(eg_ec_supported_modes))
          return -EINVAL;

   // Get detector width from the camera
   err = eg_ec_mipi_read_reg(eg_ec->i2c_client, EC_FEATURE_DETECTOR_WIDTH, (uint8_t*)&detectorWidth, sizeof(detectorWidth));
   if (!err)
   {
      fse->min_width = detectorWidth;
      fse->max_width = detectorWidth;
   }
   else
   {
      dev_err(&eg_ec->i2c_client->dev, "Failed to get detector's width.\n");
   }

   udelay(10000);

   // Get detector height from the camera
   err = eg_ec_mipi_read_reg(eg_ec->i2c_client, EC_FEATURE_DETECTOR_HEIGHT, (uint8_t*)&detectorHeight, sizeof(detectorHeight));
   if (!err)
   {
      fse->min_height = detectorHeight;
      fse->max_height = detectorHeight;
   }
   else
   {
      dev_err(&eg_ec->i2c_client->dev, "Failed to get detector's height.\n");
   }

   return 0;
}

static int eg_ec_get_pad_format(struct v4l2_subdev *sd,
      struct v4l2_subdev_state *sd_state,
      struct v4l2_subdev_format *fmt)
{
   struct eg_ec *eg_ec = to_eg_ec(sd);
   uint32_t predefinedFormat = EC_PREDEFINED_FORMAT_YCBCR;
   uint32_t detectorWidth = DEFAULT_WIDTH;
   uint32_t detectorHeight = DEFAULT_HEIGHT;
   int err;

   if (fmt->pad)
      return -EINVAL;

   if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
      struct v4l2_mbus_framefmt *try_fmt =
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
         v4l2_subdev_get_try_format(&eg_ec->sd, sd_state,
#else
         v4l2_subdev_state_get_format(sd_state,
#endif
               fmt->pad);
      fmt->format = *try_fmt;
   }
   else
   {
      // Get prededined format from the camera
      err = eg_ec_mipi_read_reg(eg_ec->i2c_client, EC_FEATURE_PREDIFINED_FORMAT, (uint8_t*)&predefinedFormat, sizeof(predefinedFormat));
      if (err)
      {
         predefinedFormat = EC_PREDEFINED_FORMAT_YCBCR;
         dev_err(&eg_ec->i2c_client->dev, "Failed to get predifined video format. Default YUYV\n");
      }

      switch (predefinedFormat)
      {
         case EC_PREDEFINED_FORMAT_YCBCR:
            eg_ec->fmt.code = MEDIA_BUS_FMT_UYVY8_1X16;
            break;
         case EC_PREDEFINED_FORMAT_RGB:
            eg_ec->fmt.code = MEDIA_BUS_FMT_RGB888_1X24;
            break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,1,63)
         case EC_PREDEFINED_FORMAT_Y16:
            eg_ec->fmt.code = MEDIA_BUS_FMT_Y16_1X16;
            break;
#endif
         default :
            eg_ec->fmt.code = MEDIA_BUS_FMT_UYVY8_1X16;
            break;
      }
      
      udelay(10000);

      // Get detector width from the camera
      err = eg_ec_mipi_read_reg(eg_ec->i2c_client, EC_FEATURE_DETECTOR_WIDTH, (uint8_t*)&detectorWidth, sizeof(detectorWidth));
      if (!err)
      {
         eg_ec->fmt.width = detectorWidth;
      }
      else
      {
         dev_err(&eg_ec->i2c_client->dev, "Failed to get detector's width.\n");
      }

      udelay(10000);

      // Get detector height from the camera
      err = eg_ec_mipi_read_reg(eg_ec->i2c_client, EC_FEATURE_DETECTOR_HEIGHT, (uint8_t*)&detectorHeight, sizeof(detectorHeight));
      if (!err)
      {
         eg_ec->fmt.height = detectorHeight;
      }
      else
      {
         dev_err(&eg_ec->i2c_client->dev, "Failed to get detector's height.\n");
      }

      fmt->format = eg_ec->fmt;
   }

   return 0;
}

static int eg_ec_set_pad_format(struct v4l2_subdev *sd,
      struct v4l2_subdev_state *sd_state,
      struct v4l2_subdev_format *fmt)
{
   struct eg_ec *eg_ec = to_eg_ec(sd);
   struct v4l2_mbus_framefmt *format;
   int i;

   if (fmt->pad)
      return -EINVAL;

   for (i = 0; i < NUM_MBUS_CODES; i++)
      if (eg_ec_mbus_codes[i] == fmt->format.code)
         break;

   if (i >= NUM_MBUS_CODES)
      i = 0;

   eg_ec->mbus_code_index = i;

   fmt->format.code = eg_ec_mbus_codes[eg_ec->mbus_code_index];
   fmt->format.field = V4L2_FIELD_NONE;
   fmt->format.colorspace = V4L2_COLORSPACE_SRGB;
   fmt->format.ycbcr_enc =
      V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->format.colorspace);
   fmt->format.quantization =
      V4L2_MAP_QUANTIZATION_DEFAULT(true, fmt->format.colorspace,
            fmt->format.ycbcr_enc);
   fmt->format.xfer_func =
      V4L2_MAP_XFER_FUNC_DEFAULT(fmt->format.colorspace);

   if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
      format = v4l2_subdev_get_try_format(&eg_ec->sd, sd_state, fmt->pad);
#else
      format = v4l2_subdev_state_get_format(sd_state, fmt->pad);
#endif
   else
      format = &eg_ec->fmt;

   *format = fmt->format;

   return 0;
}

static int eg_ec_get_selection(struct v4l2_subdev *sd,
      struct v4l2_subdev_state *sd_state,
      struct v4l2_subdev_selection *sel)
{
   struct eg_ec *eg_ec = to_eg_ec(sd);

   switch (sel->target) {
//      case V4L2_SEL_TGT_CROP:
      case V4L2_SEL_TGT_NATIVE_SIZE:
//      case V4L2_SEL_TGT_CROP_DEFAULT:
         sel->r.top = 0;
         sel->r.left = 0;
         sel->r.width = eg_ec->fmt.width;
         sel->r.height = eg_ec->fmt.height;

         return 0;
   }

   return -EINVAL;
}

static int eg_ec_set_stream(struct v4l2_subdev *sd, int enable)
{
   /*
    * Don't need to do anything here, just assume the source is streaming
    * already.
    */
   return 0;
}

static const struct v4l2_subdev_core_ops eg_ec_core_ops = {
   .subscribe_event = v4l2_ctrl_subdev_subscribe_event,
   .unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops eg_ec_video_ops = {
   .s_stream = eg_ec_set_stream,
};

static const struct v4l2_subdev_pad_ops eg_ec_pad_ops = {
   .enum_mbus_code = eg_ec_enum_mbus_code,
   .get_fmt = eg_ec_get_pad_format,
   .set_fmt = eg_ec_set_pad_format,
   .get_selection = eg_ec_get_selection,
   .enum_frame_size = eg_ec_enum_frame_size,
};

static const struct v4l2_subdev_ops eg_ec_subdev_ops = {
   .core = &eg_ec_core_ops,
   .video = &eg_ec_video_ops,
   .pad = &eg_ec_pad_ops,
};

static const struct v4l2_subdev_internal_ops eg_ec_internal_ops = {
   .open = eg_ec_open,
};

/* Initialize control handlers */
static int eg_ec_init_controls(struct eg_ec *eg_ec)
{
   struct v4l2_ctrl_handler *ctrl_hdlr;
   struct v4l2_ctrl *ctrl;
   int ret;

   ctrl_hdlr = &eg_ec->ctrl_handler;
   ret = v4l2_ctrl_handler_init(ctrl_hdlr, 4);
   if (ret)
      return ret;

   mutex_init(&eg_ec->mutex);
   ctrl_hdlr->lock = &eg_ec->mutex;

   /* By default, PIXEL_RATE is read only */
   v4l2_ctrl_new_std(ctrl_hdlr, &eg_ec_ctrl_ops, V4L2_CID_PIXEL_RATE,
         1, 1, 1, 1);

   /* Initial vblank/hblank/exposure parameters based on current mode */
   ctrl = v4l2_ctrl_new_std(ctrl_hdlr, &eg_ec_ctrl_ops, V4L2_CID_VBLANK,
         1, 1, 1, 1);
   if (ctrl)
      ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

   ctrl = v4l2_ctrl_new_std(ctrl_hdlr, &eg_ec_ctrl_ops, V4L2_CID_HBLANK,
         1, 1, 1, 1);
   if (ctrl)
      ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

   ctrl = v4l2_ctrl_new_std(ctrl_hdlr, &eg_ec_ctrl_ops, V4L2_CID_EXPOSURE,
         1, 1, 1, 1);
   if (ctrl)
      ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

   ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr, &eg_ec_ctrl_ops, V4L2_CID_LINK_FREQ,
                                0, 0, link_freq_menu_items);
   if (ctrl)
      ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;


   if (ctrl_hdlr->error) {
      ret = ctrl_hdlr->error;
      dev_err(&eg_ec->i2c_client->dev, "%s control init failed (%d)\n",
            __func__, ret);
      goto error;
   }

   eg_ec->sd.ctrl_handler = ctrl_hdlr;

   return 0;

error:
   v4l2_ctrl_handler_free(ctrl_hdlr);
   mutex_destroy(&eg_ec->mutex);

   return ret;
}

static void eg_ec_free_controls(struct eg_ec *eg_ec)
{
   v4l2_ctrl_handler_free(eg_ec->sd.ctrl_handler);
   mutex_destroy(&eg_ec->mutex);
}

static int eg_ec_check_hwcfg(struct device *dev)
{
   struct fwnode_handle *endpoint;
   struct v4l2_fwnode_endpoint ep_cfg = {
      .bus_type = V4L2_MBUS_CSI2_DPHY
   };
   int ret = -EINVAL;

   endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
   if (!endpoint) {
      dev_err(dev, "endpoint node not found\n");
      return -EINVAL;
   }

   if (v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep_cfg)) {
      dev_err(dev, "could not parse endpoint\n");
      goto error_out;
   }

   ret = 0;

error_out:
   v4l2_fwnode_endpoint_free(&ep_cfg);
   fwnode_handle_put(endpoint);

   return ret;
}

static int eg_ec_probe(struct i2c_client *client)
{
   struct device *dev = &client->dev;
   struct eg_ec *eg_ec;
   int ret;
   int i;
   int err;
   uint32_t upgradeMode;

   // Find the first i2c client available
   for (i = 0; i < MAX_I2C_CLIENTS_NUMBER; i++)
   {
      if (i2c_clients[i].i2c_client == NULL)
      {
         i2c_clients[i].i2c_client = client;
         i2c_clients[i].root_adap = i2c_root_adapter(dev);
         sprintf(i2c_clients[i].chnod_name,  "%s-%s", dev_driver_string(dev), dev_name(dev));
         dev_info(dev, "chnod: /dev/%s\n", i2c_clients[i].chnod_name);
         err = eg_ec_chnod_register_device(i);
         if (err)
         {
            dev_err(dev, "chnod register failed\n");
            i2c_clients[i].chnod_name[0] = 0;
            return err;
         }

         // Try to communicate with the camera
         err = eg_ec_mipi_read_reg(i2c_clients[i].i2c_client, 0, (uint8_t*)&upgradeMode, sizeof(upgradeMode));
         if (err)
         {
            dev_err(dev, "Failed to communicate with the camera\n");
            goto err_camera_register;
         }

         break;

      }
   }

   eg_ec = devm_kzalloc(dev, sizeof(*eg_ec), GFP_KERNEL);
   if (!eg_ec)
      return -ENOMEM;

   eg_ec->i2c_client = client;

   v4l2_subdev_init(&eg_ec->sd, &eg_ec_subdev_ops);
   /* the owner is the same as the i2c_client's driver owner */
   eg_ec->sd.owner = dev->driver->owner;
   eg_ec->sd.dev =dev;
   v4l2_set_subdevdata(&eg_ec->sd, client);

   /* initialize name */
   snprintf(eg_ec->sd.name, sizeof(eg_ec->sd.name), "%s",
         dev->driver->name);

   /* Check the hardware configuration in device tree */
   if (eg_ec_check_hwcfg(dev))
      return -EINVAL;

   eg_ec->fmt.width = eg_ec_supported_modes[0].width;
   eg_ec->fmt.height = eg_ec_supported_modes[0].height;
   eg_ec->fmt.code = eg_ec_mbus_codes[0];
   eg_ec->fmt.field = V4L2_FIELD_NONE;
   eg_ec->fmt.colorspace = V4L2_COLORSPACE_SRGB;
//   eg_ec->fmt.ycbcr_enc =
//      V4L2_MAP_YCBCR_ENC_DEFAULT(eg_ec->fmt.colorspace);
//   eg_ec->fmt.quantization =
//      V4L2_MAP_QUANTIZATION_DEFAULT(true,
//            eg_ec->fmt.colorspace,
//            eg_ec->fmt.ycbcr_enc);
//   eg_ec->fmt.xfer_func =
//      V4L2_MAP_XFER_FUNC_DEFAULT(eg_ec->fmt.colorspace);

   ret = eg_ec_init_controls(eg_ec);
   if (ret)
      return ret;

   /* Initialize subdev */
   eg_ec->sd.internal_ops = &eg_ec_internal_ops;
   eg_ec->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
      V4L2_SUBDEV_FL_HAS_EVENTS;
   eg_ec->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

   /* Initialize source pads */
   eg_ec->pad.flags = MEDIA_PAD_FL_SOURCE;

   ret = media_entity_pads_init(&eg_ec->sd.entity, 1,
         &eg_ec->pad);
   if (ret) {
      dev_err(dev, "failed to init entity pads: %d\n", ret);
      goto error_handler_free;
   }

   ret = v4l2_async_register_subdev_sensor(&eg_ec->sd);
   if (ret < 0) {
      dev_err(dev, "failed to register eg_ec sub-device: %d\n", ret);
      goto error_media_entity;
   }

   dev_info(dev, "registered\n");

   return 0;

error_media_entity:
   media_entity_cleanup(&eg_ec->sd.entity);

error_handler_free:
   eg_ec_free_controls(eg_ec);

err_camera_register:
   device_destroy(i2c_clients[i].pClass_chnod, i2c_clients[i].chnod_device_number);
   class_destroy(i2c_clients[i].pClass_chnod);
   unregister_chrdev(i2c_clients[i].chnod_major_number, i2c_clients[i].chnod_name);
   i2c_clients[i].chnod_name[0] = 0;

   return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,1,1)
static int eg_ec_remove(struct i2c_client *client)
#else
static void eg_ec_remove(struct i2c_client *client)
#endif
{
   struct v4l2_subdev *sd = i2c_get_clientdata(client);
   struct device *dev = &client->dev;
   struct eg_ec *eg_ec = to_eg_ec(sd);
   int i;

   v4l2_async_unregister_subdev(&eg_ec->sd);
   media_entity_cleanup(&eg_ec->sd.entity);
   eg_ec_free_controls(eg_ec);

   for (i = 0; i < MAX_I2C_CLIENTS_NUMBER; i++)
   {
      if (i2c_clients[i].chnod_name[0] != 0)
      {
         if(i2c_clients[i].chnod_major_number != 0)
         {
            device_destroy(i2c_clients[i].pClass_chnod, i2c_clients[i].chnod_device_number);
            class_destroy(i2c_clients[i].pClass_chnod);
            unregister_chrdev(i2c_clients[i].chnod_major_number, i2c_clients[i].chnod_name);
         }
         dev_info(dev, "Removed %s device\n", i2c_clients[i].chnod_name);
         i2c_clients[i].i2c_client = NULL;
         i2c_clients[i].chnod_name[0] = 0;
         break;
      }
   }


#if LINUX_VERSION_CODE <= KERNEL_VERSION(6,1,1)
   return 0;
#endif
}

static const struct of_device_id eg_ec_dt_ids[] = {
   { .compatible = "xenics,eg-ec-mipi" },
   { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, eg_ec_dt_ids);

static struct i2c_driver eg_ec_driver = {
#if LINUX_VERSION_CODE <= KERNEL_VERSION(6,2,0)
   .probe_new = eg_ec_probe,
#else   
   .probe = eg_ec_probe,
#endif
   .remove = eg_ec_remove,
   .driver = {
      .name = "eg-ec-i2c",
      .of_match_table	= eg_ec_dt_ids,
   },
};

module_i2c_driver(eg_ec_driver);

MODULE_AUTHOR("Xenics Exosens");
MODULE_DESCRIPTION("Xenics Exosens MIPI camera I2C driver for EngineCore cameras");
MODULE_LICENSE("GPL v2");
