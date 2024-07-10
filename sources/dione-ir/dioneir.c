// SPDX-License-Identifier: GPL-2.0
/*
 * A V4L2 driver for Xenics Exosens MIPI cameras.
 *
 * Based on Sony dummy dione_ir driver
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
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/clk.h>

#include "tc358746_regs.h"
#include "tc358746_calculation.h"

#define MAX_I2C_CLIENTS_NUMBER 128

#define DIONE_IR_REG_WIDTH_MAX		0x0002f028
#define DIONE_IR_REG_HEIGHT_MAX		0x0002f02c
#define DIONE_IR_REG_MODEL_NAME		0x00000044
#define DIONE_IR_REG_FIRMWARE_VERSION	0x2000e000
#define DIONE_IR_REG_ACQUISITION_STOP	0x00080104
#define DIONE_IR_REG_ACQUISITION_SRC	0x00080108
#define DIONE_IR_REG_ACQUISITION_STAT	0x0008010c

/* #define DIONE_IR_I2C_TMO_MS		5 */
/* #define DIONE_IR_STARTUP_TMO_MS		1500 */
/* #define DIONE_IR_HAS_SYSFS		1 */

#define CSI_HSTXVREGCNT			5


static int test_mode = 0;
static int quick_mode = 1;
module_param(test_mode, int, 0644);
module_param(quick_mode, int, 0644);

s64 link_freq_menu_items[1];

/* Array of all the mbus formats that we'll accept */
u32 dione_ir_mbus_codes[] = {
   MEDIA_BUS_FMT_BGR888_1X24,
};
#define NUM_MBUS_CODES ARRAY_SIZE(dione_ir_mbus_codes)


struct dione_ir_i2c_client {
   struct i2c_client *i2c_client;
   struct i2c_adapter *root_adap;
   char chnod_name[128];
   int i2c_locked ;
   int chnod_major_number;
   dev_t chnod_device_number;
   struct class *pClass_chnod;
};

struct dione_ir_i2c_client i2c_clients[MAX_I2C_CLIENTS_NUMBER];

struct dione_ir {
	struct i2c_client		*tc35_client;
	struct i2c_client		*fpga_client;
	struct regmap			*ctl_regmap;
	struct regmap			*tx_regmap;
   struct v4l2_subdev sd;
   struct media_pad pad;

   struct v4l2_mbus_framefmt fmt;

   struct v4l2_ctrl_handler ctrl_handler;
   /*
    * Mutex for serialized access:
    * Protect dione_ir module set pad format and start/stop streaming safely.
    */
   struct mutex mutex;

	int				quick_mode;
#ifdef DIONE_IR_STARTUP_TMO_MS
	ktime_t				start_up;
#endif
	bool				tc35_found;
	bool				fpga_found;
	int				mode;
   int            mbus_code_index;

	u32				*fpga_address;
	unsigned int	fpga_address_num;

	u64				*link_frequencies;
	unsigned int	link_frequencies_num;

   struct clk *clk;
   u32 def_clk_freq;

   struct v4l2_ctrl *hblank;
};

/* Mode : resolution and related config&values */
struct dione_ir_mode {
        u32 width; // Frame width in pixels
        u32 height; // Frame height in pixels
        u32 line_length; // Line length in pixels
        u32 pix_clk_hz; // Pixel clock in Hz
};


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
	.name = "tc358746-ctl",
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
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.rd_table = &tx_regmap_access,
	.wr_table = &tx_regmap_access,
	.name = "tc358746-tx",
};

struct v4l2_fwnode_endpoint dione_ir_ep_cfg = {
   .bus_type = V4L2_MBUS_CSI2_DPHY
};

static const struct dione_ir_mode dione_ir_supported_modes[] = {
        {
               .width = 640,
               .height = 480,
               .line_length = 694,
               .pix_clk_hz = 20000000,
        },
        {
               .width = 1280,
               .height = 1024,
               .line_length = 1334,
               .pix_clk_hz = 83000000,
        },
        {
               .width = 1280,
               .height = 1024,
               .line_length = 1334,
               .pix_clk_hz = 83000000,
        },
        {
               .width = 320,
               .height = 240,
               .line_length = 1404,
               .pix_clk_hz = 20000000,
        },
};

int dione_ir_chnod_open (struct inode * pInode, struct file * file);
int dione_ir_chnod_release (struct inode * pInode, struct file * file);

static void dione_ir_regmap_format_32_ble(void *buf, unsigned int val)
{
   u8 *b = buf;
   int val_after;

   b[0] = val >> 8;
   b[1] = val;
   b[2] = val >> 24;
   b[3] = val >> 16;
   val_after = *(int*)buf;
}

static int dione_ir_find_frmfmt(u32 width, u32 height)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(dione_ir_supported_modes); i++) {
		if (dione_ir_supported_modes[i].width == width && dione_ir_supported_modes[i].height == height)
			return i;
	}

	return -1;
}


static inline struct dione_ir *to_dione_ir(struct v4l2_subdev *_sd)
{
   return container_of(_sd, struct dione_ir, sd);
}

#ifdef DIONE_IR_I2C_TMO_MS
static inline int i2c_transfer_one(struct i2c_client *client,
				   void *buf, size_t len, u16 flags)
{
	struct i2c_msg msgs;

	msgs.addr = client->addr;
	msgs.flags = flags;
	msgs.len = len;
	msgs.buf = buf;

	return i2c_transfer(client->adapter, &msgs, 1);
}

static int dione_ir_i2c_read(struct i2c_client *client, u32 reg, u8 *dst, u16 len)
{
	u8 tx_data[6];
	u8 rx_data[72];
	int ret = 0, tmo, retry;

	if (len > sizeof(rx_data) - 2)
		ret = -EINVAL;

	if (!ret) {
		*(u32 *)tx_data = cpu_to_le32(reg);
		*(u16 *)(tx_data + 4) = cpu_to_le16(len);

		retry = 4;
		tmo = DIONE_IR_I2C_TMO_MS;
		ret = -EIO;

		while (retry-- > 0) {
			if (i2c_transfer_one(client, tx_data, 6, 0) == 1) {
				ret = 0;
				break;
			}
			msleep(tmo);
			tmo <<= 2;
		}
	}

	if (!ret) {
		retry = 4;
		tmo = DIONE_IR_I2C_TMO_MS;
		ret = -EIO;

		msleep(2);
		while (retry-- > 0) {
			if (i2c_transfer_one(client,
					     rx_data, len + 2, I2C_M_RD) == 1) {
				ret = 0;
				break;
			}
			msleep(tmo);
			tmo <<= 2;
		}
	}

	if (!ret) {
		if (rx_data[0] != 0 || rx_data[1] != 0) {
			ret = -EINVAL;
		} else {
			switch (len) {
			case 1:
				dst[0] = rx_data[2];
				break;
			case 2:
				*(u16 *)dst = le16_to_cpu(*(u16 *)(rx_data + 2));
				break;
			case 4:
				*(u32 *)dst = le32_to_cpu(*(u32 *)(rx_data + 2));
				break;
			default:
				memcpy(dst, rx_data + 2, len);
			}
		}
	}

	return ret;
}

static int dione_ir_i2c_write32(struct i2c_client *client, u32 reg, u32 val)
{
	int ret = -EIO, retry = 4, tmo = DIONE_IR_I2C_TMO_MS;
	u8 tx_data[10];

	*(u32 *)tx_data = cpu_to_le32(reg);
	*(u16 *)(tx_data + 4) = cpu_to_le16(4);
	*(u32 *)(tx_data + 6) = cpu_to_le32(val);

	while (retry-- > 0) {
		if (i2c_transfer_one(client, tx_data, sizeof(tx_data), 0) == 1) {
			ret = 0;
			break;
		}
		msleep(tmo);
		tmo <<= 2;
	}

	return ret;
}
#else
static int dione_ir_i2c_read(struct i2c_client *client, u32 reg, u8 *dst, u16 len)
{
	struct i2c_msg msgs[2];
	u8 tx_data[6];
	u8 rx_data[72];
	int ret = 0;

	if (len > sizeof(rx_data) - 2)
		ret = -EINVAL;

	if (!ret) {
		*(u32 *)tx_data = cpu_to_le32(reg);
		*(u16 *)(tx_data + 4) = cpu_to_le16(len);

		msgs[0].addr = client->addr;
		msgs[0].flags = 0;
		msgs[0].len = sizeof(tx_data);
		msgs[0].buf = tx_data;

		msgs[1].addr = client->addr;
		msgs[1].flags = I2C_M_RD;
		msgs[1].len = len + 2;
		msgs[1].buf = rx_data;

		if (i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs)) != 2)
			ret = -EIO;
	}

	if (!ret) {
		if (rx_data[0] != 0 || rx_data[1] != 0) {
			ret = -EINVAL;
		} else {
			switch (len) {
			case 1:
				dst[0] = rx_data[2];
				break;
			case 2:
				*(u16 *)dst = le16_to_cpu(*(u16 *)(rx_data + 2));
				break;
			case 4:
				*(u32 *)dst = le32_to_cpu(*(u32 *)(rx_data + 2));
				break;
			default:
				memcpy(dst, rx_data + 2, len);
			}
		}
	}

	return ret;
}

static int dione_ir_i2c_write32(struct i2c_client *client, u32 reg, u32 val)
{
	struct i2c_msg msgs;
	u8 tx_data[10];

	*(u32 *)tx_data = cpu_to_le32(reg);
	*(u16 *)(tx_data + 4) = cpu_to_le16(4);
	*(u32 *)(tx_data + 6) = cpu_to_le32(val);

	msgs.addr = client->addr;
	msgs.flags = 0;
	msgs.len = sizeof(tx_data);
	msgs.buf = tx_data;

	if (i2c_transfer(client->adapter, &msgs, 1) != 1)
		return -EIO;

	return 0;
}
#endif


static inline int tc358746_sleep_mode(struct regmap *regmap, int enable)
{
	return regmap_update_bits(regmap, SYSCTL, SYSCTL_SLEEP_MASK,
				  enable ? SYSCTL_SLEEP_MASK : 0);
}

static inline int tc358746_sreset(struct regmap *regmap)
{
	int err;

	err = regmap_write(regmap, SYSCTL, SYSCTL_SRESET_MASK);

	udelay(10);

	if (!err)
		err = regmap_write(regmap, SYSCTL, 0);

	return err;
}

static int tc358746_set_pll(struct regmap *regmap,
			    const struct tc358746_pll *pll,
			    const struct tc358746_csi *csi)
{
	u32 pllctl0, pllctl1, pllctl0_new;
	int err;

	err = regmap_read(regmap, PLLCTL0, &pllctl0);
	if (!err)
		err = regmap_read(regmap, PLLCTL1, &pllctl1);

	if (err)
		return err;

	pllctl0_new = PLLCTL0_PLL_PRD_SET(pll->pll_prd) |
	  PLLCTL0_PLL_FBD_SET(pll->pll_fbd);

	/*
	 * Only rewrite when needed (new value or disabled), since rewriting
	 * triggers another format change event.
	 */
	if (pllctl0 != pllctl0_new || (pllctl1 & PLLCTL1_PLL_EN_MASK) == 0) {
		u16 pllctl1_mask = PLLCTL1_PLL_FRS_MASK | PLLCTL1_RESETB_MASK |
				   PLLCTL1_PLL_EN_MASK;
		u16 pllctl1_val = PLLCTL1_PLL_FRS_SET(csi->speed_range) |
				  PLLCTL1_RESETB_MASK | PLLCTL1_PLL_EN_MASK;

		err = regmap_write(regmap, PLLCTL0, pllctl0_new);
		if (!err)
			err = regmap_update_bits(regmap, PLLCTL1,
						 pllctl1_mask, pllctl1_val);

		udelay(1000);

		if (!err)
			err = regmap_update_bits(regmap, PLLCTL1,
						 PLLCTL1_CKEN_MASK,
						 PLLCTL1_CKEN_MASK);
	}

	return err;
}

static int tc358746_set_csi_color_space(struct regmap *regmap,
					const struct tc358746_mbus_fmt *format)
{
	int err;

	err = regmap_update_bits(regmap, DATAFMT,
				 (DATAFMT_PDFMT_MASK | DATAFMT_UDT_EN_MASK),
				 DATAFMT_PDFMT_SET(format->pdformat));

	if (!err)
		err = regmap_update_bits(regmap, CONFCTL, CONFCTL_PDATAF_MASK,
					 CONFCTL_PDATAF_SET(format->pdataf));

	return err;
}

static int tc358746_set_buffers(struct regmap *regmap,
				u32 width, u8 bpp, u16 vb_fifo)
{
	unsigned int byte_per_line = (width * bpp) / 8;
	int err;

	err = regmap_write(regmap, FIFOCTL, vb_fifo);

	if (!err)
		err = regmap_write(regmap, WORDCNT, byte_per_line);

	return err;
}

static int tc358746_enable_csi_lanes(struct regmap *regmap,
				     int lane_num, int enable)
{
	u32 val = 0;
	u32 bleVal = 0;
	int err = 0;

	if (lane_num < 1 || !enable) {
		if (!err)
      {
         dione_ir_regmap_format_32_ble((void *)&bleVal, CLW_CNTRL_CLW_LANEDISABLE_MASK);
			err = regmap_write(regmap, CLW_CNTRL, bleVal);
      }
		if (!err)
      {
         dione_ir_regmap_format_32_ble((void *)&bleVal, D0W_CNTRL_D0W_LANEDISABLE_MASK);
			err = regmap_write(regmap, D0W_CNTRL, bleVal);
      }
	}

	if (lane_num < 2 || !enable) {
		if (!err)
      {
         dione_ir_regmap_format_32_ble((void *)&bleVal, D1W_CNTRL_D1W_LANEDISABLE_MASK);
			err = regmap_write(regmap, D1W_CNTRL, bleVal);
      }
	}

	if (lane_num < 3 || !enable) {
		if (!err)
      {
         dione_ir_regmap_format_32_ble((void *)&bleVal, D2W_CNTRL_D2W_LANEDISABLE_MASK);
			err = regmap_write(regmap, D2W_CNTRL, bleVal);
      }
	}

	if (lane_num < 4 || !enable) {
		if (!err)
      {
         dione_ir_regmap_format_32_ble((void *)&bleVal, D2W_CNTRL_D3W_LANEDISABLE_MASK);
			err = regmap_write(regmap, D3W_CNTRL, bleVal);
      }
	}

	if (lane_num > 0 && enable) {
		val |= HSTXVREGEN_CLM_HSTXVREGEN_MASK |
			HSTXVREGEN_D0M_HSTXVREGEN_MASK;
	}

	if (lane_num > 1 && enable)
		val |= HSTXVREGEN_D1M_HSTXVREGEN_MASK;

	if (lane_num > 2 && enable)
		val |= HSTXVREGEN_D2M_HSTXVREGEN_MASK;

	if (lane_num > 3 && enable)
		val |= HSTXVREGEN_D3M_HSTXVREGEN_MASK;

	if (!err)
   {
      dione_ir_regmap_format_32_ble((void *)&bleVal, val);
		err = regmap_write(regmap, HSTXVREGEN, bleVal);
   }

	return err;
}

static int tc358746_set_csi(struct regmap *regmap,
			    const struct tc358746_csi *csi)
{
	u32 val, bleVal;
	int err;

	val = TCLK_HEADERCNT_TCLK_ZEROCNT_SET(csi->tclk_zerocnt) |
	      TCLK_HEADERCNT_TCLK_PREPARECNT_SET(csi->tclk_preparecnt);
   dione_ir_regmap_format_32_ble((void *)&bleVal, val);
	err = regmap_write(regmap, TCLK_HEADERCNT, bleVal);

	val = THS_HEADERCNT_THS_ZEROCNT_SET(csi->ths_zerocnt) |
	      THS_HEADERCNT_THS_PREPARECNT_SET(csi->ths_preparecnt);
	if (!err)
   {
      dione_ir_regmap_format_32_ble((void *)&bleVal, val);
		err = regmap_write(regmap, THS_HEADERCNT, bleVal);
   }

	if (!err)
   {
      dione_ir_regmap_format_32_ble((void *)&bleVal, csi->twakeupcnt);
		err = regmap_write(regmap, TWAKEUP, bleVal);
   }

	if (!err)
   {
      dione_ir_regmap_format_32_ble((void *)&bleVal, csi->tclk_postcnt);
		err = regmap_write(regmap, TCLK_POSTCNT, bleVal);
   }

	if (!err)
   {
      dione_ir_regmap_format_32_ble((void *)&bleVal, csi->ths_trailcnt);
		err = regmap_write(regmap, THS_TRAILCNT, bleVal);
   }

	if (!err)
   {
      dione_ir_regmap_format_32_ble((void *)&bleVal, csi->lineinitcnt);
		err = regmap_write(regmap, LINEINITCNT, bleVal);
   }

	if (!err)
   {
      dione_ir_regmap_format_32_ble((void *)&bleVal, csi->lptxtimecnt);
		err = regmap_write(regmap, LPTXTIMECNT, bleVal);
   }

	if (!err)
   {
      dione_ir_regmap_format_32_ble((void *)&bleVal, csi->tclk_trailcnt);
		err = regmap_write(regmap, TCLK_TRAILCNT, bleVal);
   }

	if (!err)
   {
      dione_ir_regmap_format_32_ble((void *)&bleVal, CSI_HSTXVREGCNT);
		err = regmap_write(regmap, HSTXVREGCNT, bleVal);
   }

	val = csi->is_continuous_clk ? TXOPTIONCNTRL_CONTCLKMODE_MASK : 0;
	if (!err)
   {
      dione_ir_regmap_format_32_ble((void *)&bleVal, val);
		err = regmap_write(regmap, TXOPTIONCNTRL, bleVal);
   }

	return err;
}

static int tc358746_wr_csi_control(struct regmap *regmap, u32 val)
{
	u32 bleVal;
	val &= CSI_CONFW_DATA_MASK;
	val |= CSI_CONFW_MODE_SET_MASK | CSI_CONFW_ADDRESS_CSI_CONTROL_MASK;
   dione_ir_regmap_format_32_ble((void *)&bleVal, val);

	return regmap_write(regmap, CSI_CONFW, bleVal);
}

static int tc358746_enable_csi_module(struct regmap *regmap, int lane_num)
{
	u32 val, bleVal;
	int err;

   dione_ir_regmap_format_32_ble((void *)&bleVal, STARTCNTRL_START_MASK);
	err = regmap_write(regmap, STARTCNTRL, bleVal);

	if (!err)
   {
      dione_ir_regmap_format_32_ble((void *)&bleVal, CSI_START_STRT_MASK);
		err = regmap_write(regmap, CSI_START, bleVal);
   }

	val = CSI_CONTROL_NOL_1_MASK;
	if (lane_num == 2)
		val = CSI_CONTROL_NOL_2_MASK;
	else if (lane_num == 3)
		val = CSI_CONTROL_NOL_3_MASK;
	else if (lane_num == 4)
		val = CSI_CONTROL_NOL_4_MASK;

	val |= CSI_CONTROL_CSI_MODE_MASK | CSI_CONTROL_TXHSMD_MASK |
		CSI_CONTROL_EOTDIS_MASK; /* add, according to Excel */

	if (!err)
   {
		err = tc358746_wr_csi_control(regmap, val);
   }

	return err;
}

static int dione_ir_set_mode(struct dione_ir *priv)
{
	struct regmap *ctl_regmap = priv->ctl_regmap;
	struct regmap *tx_regmap = priv->tx_regmap;
	struct tc358746_input input;
	struct tc358746 params;
	int i, err;
	struct device *dev = &priv->tc35_client->dev;

	input.mbus_fmt = dione_ir_mbus_codes[priv->mbus_code_index]; // TODO support multpible codes
	input.refclk = priv->def_clk_freq;
	input.num_lanes = dione_ir_ep_cfg.bus.mipi_csi2.num_data_lanes;
   input.discontinuous_clk = dione_ir_ep_cfg.bus.mipi_csi2.flags & V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK ? 1 : 0;
	input.pclk = dione_ir_supported_modes[priv->mode].pix_clk_hz;
	input.width = dione_ir_supported_modes[priv->mode].width;
	input.hblank = dione_ir_supported_modes[priv->mode].line_length - dione_ir_supported_modes[priv->mode].width;

	for (i = 0; i < dione_ir_ep_cfg.nr_of_link_frequencies; i++) {
		input.link_frequency = dione_ir_ep_cfg.link_frequencies[i];
		if (tc358746_calculate(&params, &input) == 0)
			break;
	}

	if (i >= dione_ir_ep_cfg.nr_of_link_frequencies) {
		dev_err(dev, "could not calculate parameters for tc358746\n");
		return -EINVAL;
	}

	err = 0;
	if (test_mode) {
#ifdef DIONE_IR_STARTUP_TMO_MS
		/* wait until FPGA in sensor finishes booting up */
		while (ktime_ms_delta(ktime_get(), priv->start_up)
				< DIONE_IR_STARTUP_TMO_MS)
			msleep(100);
#endif
		/* enable test pattern in the sensor module */
		err = dione_ir_i2c_write32(priv->fpga_client,
					   DIONE_IR_REG_ACQUISITION_STOP, 2);
		if (!err) {
			msleep(300);
			err = dione_ir_i2c_write32(priv->fpga_client,
						   DIONE_IR_REG_ACQUISITION_SRC, 0);
		}

		if (!err) {
			msleep(300);
			err = dione_ir_i2c_write32(priv->fpga_client,
						   DIONE_IR_REG_ACQUISITION_STOP, 1);
		}
	}

	regmap_write(ctl_regmap, DBG_ACT_LINE_CNT, 0);

	if (!err)
		err = tc358746_sreset(ctl_regmap);
	if (err) {
		dev_err(dev, "Failed to reset chip\n");
		return err;
	}

	err = tc358746_set_pll(ctl_regmap, &params.pll, &params.csi);
	if (err) {
		dev_err(dev, "Failed to setup PLL\n");
		return err;
	}

	err = tc358746_set_csi_color_space(ctl_regmap, params.format);

	if (!err)
		err = tc358746_set_buffers(ctl_regmap, input.width,
				           params.format->bpp, params.vb_fifo);

	if (!err)
		err = tc358746_enable_csi_lanes(tx_regmap,
						params.csi.lane_num, true);

	if (!err)
		err = tc358746_set_csi(tx_regmap, &params.csi);

	if (!err)
		err = tc358746_enable_csi_module(tx_regmap,
						 params.csi.lane_num);

	if (err)
		dev_err(dev, "%s return code (%d)\n", __func__, err);

	return err;
}

static ssize_t dione_ir_chnod_read(
      struct file *file_ptr
      , char __user *user_buffer
      , size_t count
      , loff_t *position)
{
   int ret = -EINVAL;
   int i;
   u8 *buffer_i2c = NULL;

   // printk( KERN_NOTICE "chnod: Device file read at offset = %i, bytes count = %u\n"
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

static ssize_t dione_ir_chnod_write(
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

int dione_ir_chnod_open (struct inode * pInode, struct file * file)
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

int dione_ir_chnod_release (struct inode * pInode, struct file * file)
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

static struct file_operations dione_ir_chnod_register_fops = 
{
   .owner   = THIS_MODULE,
   .read    = dione_ir_chnod_read,
   .write   = dione_ir_chnod_write,
   .open    = dione_ir_chnod_open,
   .release = dione_ir_chnod_release,
};

static inline int dione_ir_chnod_register_device(int i2c_ind)
{
   struct device *pDev;
   int result = 0;
   result = register_chrdev( 0, i2c_clients[i2c_ind].chnod_name, &dione_ir_chnod_register_fops );
   if( result < 0 )
   {
      printk( KERN_WARNING "dal register chnod:  can\'t register character device with error code = %i\n", result );
      return result;
   }
   i2c_clients[i2c_ind].chnod_major_number = result;
   printk( KERN_DEBUG "dal register chnod: registered character device with major number = %i and minor numbers 0...255\n", i2c_clients[i2c_ind].chnod_major_number );

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

static int detect_dione_ir(struct dione_ir *priv, u32 fpga_addr)
{
	struct device *dev = &priv->tc35_client->dev;
	u32 width, height;
	u8 buf[64];
	int i, mode = 0, ret;
   int err = 0;

	msleep(200);

	dev_dbg(dev, "probing fpga at address %#02x\n", fpga_addr);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,5,0)
	priv->fpga_client = i2c_new_dummy(priv->tc35_client->adapter, fpga_addr);
#else
	priv->fpga_client = i2c_new_dummy_device(priv->tc35_client->adapter, fpga_addr);
#endif
	if (!priv->fpga_client)
		return -ENOMEM;

	ret = dione_ir_i2c_read(priv->fpga_client, DIONE_IR_REG_WIDTH_MAX,
				(u8 *)&width, sizeof(width));
	if (ret < 0)
		goto error;

	ret = dione_ir_i2c_read(priv->fpga_client, DIONE_IR_REG_HEIGHT_MAX,
				(u8 *)&height, sizeof(height));
	if (ret < 0)
		goto error;

	mode = dione_ir_find_frmfmt(width, height);
	if (mode < 0) {
		ret = -ENODEV;
		goto error;
	}

	priv->fpga_found = true;

	ret = dione_ir_i2c_read(priv->fpga_client, DIONE_IR_REG_FIRMWARE_VERSION,
				buf, sizeof(buf));
	if (ret < 0)
		goto error;

	for (i = sizeof(buf) - 1; i >= 0 && buf[i] == 0xff; i--)
		buf[i] = '\0';

   if (priv->fpga_found)
   {
      // Find the first i2c client available
      for (i = 0; i < MAX_I2C_CLIENTS_NUMBER; i++)
      {
         if (i2c_clients[i].i2c_client == NULL)
         {
            i2c_clients[i].i2c_client = priv->fpga_client;
            sprintf(i2c_clients[i].chnod_name,  "%s-i2c-%s-%02x", dev_driver_string(dev),  dev_name(dev), fpga_addr);
            dev_info(dev, "chnod: /dev/%s\n", i2c_clients[i].chnod_name);
            err = dione_ir_chnod_register_device(i);
            if (err)
            {
               dev_err(dev, "chnod register failed\n");
               i2c_clients[i].chnod_name[0] = 0;
               return err;
            }
            break;
         }
      }
   }
	dev_info(dev, "dione-ir %ux%u at address %#02x, firmware: %s\n",
		 width, height, fpga_addr, buf);

	return mode;

error:
	if (priv->fpga_client != NULL) {
		i2c_unregister_device(priv->fpga_client);
		priv->fpga_client = NULL;
	}

	return ret;
}

static int dione_ir_board_setup(struct dione_ir *priv)
{
	struct device *dev = &priv->tc35_client->dev;
	struct regmap *ctl_regmap = priv->ctl_regmap;
	u32 reg_val;
	int i, _quick_mode, err = 0;

	_quick_mode = priv->quick_mode;
	priv->quick_mode = 0;
	priv->quick_mode = _quick_mode;

#ifdef DIONE_IR_STARTUP_TMO_MS
	priv->start_up = ktime_get();
#endif
	// Probe sensor model id registers
	err = regmap_read(ctl_regmap, CHIPID, &reg_val);
	if (err)
		goto done;

	if ((reg_val & CHIPID_CHIPID_MASK) != 0x4400) {
		dev_err(dev, "%s: invalid tc35 chip-id: %#x\n",
			__func__, reg_val);
		err = -ENODEV;
		goto done;
	}

	priv->tc35_found = true;

	if (err) {
		dev_err(dev, "error during power on sensor (%d)\n", err);
		goto done;
	}

	for (i = 0; i < priv->fpga_address_num; i++) {
		u32 fpga_addr = priv->fpga_address[i];
		err = detect_dione_ir(priv, fpga_addr);
		if (err >= 0) {
			priv->mode = err;
			err = 0;
			break;
		}
	}

	if (err < 0)
		goto done;

done:
	return err;
}


static int dione_ir_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
   struct dione_ir *dione_ir = to_dione_ir(sd);
   struct v4l2_mbus_framefmt *try_img_fmt =
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
      v4l2_subdev_get_try_format(sd, fh->state, 0);
#else
      v4l2_subdev_state_get_format(fh->state, 0);
#endif
   struct v4l2_rect *try_crop;

   *try_img_fmt = dione_ir->fmt;

   // Initialize try_crop rectangle
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

static int dione_ir_set_ctrl(struct v4l2_ctrl *ctrl)
{
   struct dione_ir *dione_ir =
      container_of(ctrl->handler, struct dione_ir, ctrl_handler);
   int ret = 0;
	struct device *dev = &dione_ir->tc35_client->dev;

   switch (ctrl->id) {
      default:
         dev_info(dev,
               "ctrl(id:0x%x,val:0x%x) is not handled\n",
               ctrl->id, ctrl->val);
         ret = -EINVAL;
         break;
   }

   return ret;
}

static const struct v4l2_ctrl_ops sensor_ctrl_ops = {
   .s_ctrl = dione_ir_set_ctrl,
};

static int dione_ir_enum_mbus_code(struct v4l2_subdev *sd,
      struct v4l2_subdev_state *sd_state,
      struct v4l2_subdev_mbus_code_enum *code)
{
   // struct dione_ir *dione_ir = to_dione_ir(sd);
	// struct device *dev = &dione_ir->tc35_client->dev;

   if (code->index >= NUM_MBUS_CODES)
      return -EINVAL;
   if (code->pad)
      return -EINVAL;

   code->code = dione_ir_mbus_codes[code->index];

   return 0;
}

static int dione_ir_enum_frame_size(struct v4l2_subdev *sd,
      struct v4l2_subdev_state *sd_state,
      struct v4l2_subdev_frame_size_enum *fse)
{
   // struct dione_ir *dione_ir = to_dione_ir(sd);
	// struct device *dev = &dione_ir->tc35_client->dev;

   if (fse->index)
      return -EINVAL;
   if (fse->pad)
      return -EINVAL;

   if (fse->index >= ARRAY_SIZE(dione_ir_supported_modes))
          return -EINVAL;

   fse->min_width = dione_ir_supported_modes[fse->index].width;
   fse->max_width = fse->min_width;
   fse->min_height = dione_ir_supported_modes[fse->index].height;
   fse->max_height = fse->min_height;

   return 0;
}

static int dione_ir_get_pad_format(struct v4l2_subdev *sd,
      struct v4l2_subdev_state *sd_state,
      struct v4l2_subdev_format *fmt)
{
   struct dione_ir *dione_ir = to_dione_ir(sd);
	// struct device *dev = &dione_ir->tc35_client->dev;

   if (fmt->pad)
      return -EINVAL;

   if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
      struct v4l2_mbus_framefmt *try_fmt =
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
         v4l2_subdev_get_try_format(&dione_ir->sd, sd_state,
#else
         v4l2_subdev_state_get_format(sd_state,
#endif
               fmt->pad);
      fmt->format = *try_fmt;
   }
   else
   {
      dione_ir->fmt.code = dione_ir_mbus_codes[dione_ir->mbus_code_index]; // TODO support multiple formats
      dione_ir->fmt.width = dione_ir_supported_modes[dione_ir->mode].width;
      dione_ir->fmt.height = dione_ir_supported_modes[dione_ir->mode].height;

      fmt->format = dione_ir->fmt;
   }

   return 0;
}

static int dione_ir_set_pad_format(struct v4l2_subdev *sd,
      struct v4l2_subdev_state *sd_state,
      struct v4l2_subdev_format *fmt)
{
   struct dione_ir *dione_ir = to_dione_ir(sd);
   struct v4l2_mbus_framefmt *format;
   int i;
   // const struct dione_ir_mode *mode;
	// struct device *dev = &dione_ir->tc35_client->dev;

   if (fmt->pad)
      return -EINVAL;

   for (i = 0; i < NUM_MBUS_CODES; i++)
      if (dione_ir_mbus_codes[i] == fmt->format.code)
         break;

   if (i >= NUM_MBUS_CODES)
      i = 0;

   dione_ir->mbus_code_index = i;

   fmt->format.code = dione_ir_mbus_codes[dione_ir->mbus_code_index];
   fmt->format.field = V4L2_FIELD_NONE;
   fmt->format.colorspace = V4L2_COLORSPACE_SRGB;
   // fmt->format.ycbcr_enc =
      // V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->format.colorspace);
   // fmt->format.quantization =
      // V4L2_MAP_QUANTIZATION_DEFAULT(true, fmt->format.colorspace,
            // fmt->format.ycbcr_enc);
   // fmt->format.xfer_func =
      // V4L2_MAP_XFER_FUNC_DEFAULT(fmt->format.colorspace);

   // TODO
   // mode = v4l2_find_nearest_size(dione_ir_supported_modes,
                   // ARRAY_SIZE(dione_ir_supported_modes), width, height,
                   // fmt->format.width, fmt->format.height);
   // fmt->format.width = mode->width;
   // fmt->format.height = mode->height;

   if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
      format = v4l2_subdev_get_try_format(&dione_ir->sd, sd_state, fmt->pad);
#else
      format = v4l2_subdev_state_get_format(sd_state, fmt->pad);
#endif
   else
      format = &dione_ir->fmt;

   *format = fmt->format;

   return 0;
}

static int dione_ir_get_selection(struct v4l2_subdev *sd,
      struct v4l2_subdev_state *sd_state,
      struct v4l2_subdev_selection *sel)
{
   struct dione_ir *dione_ir = to_dione_ir(sd);
	// struct device *dev = &dione_ir->tc35_client->dev;

   switch (sel->target) {
      // case V4L2_SEL_TGT_CROP:
      case V4L2_SEL_TGT_NATIVE_SIZE:
//      case V4L2_SEL_TGT_CROP_DEFAULT:
//      case V4L2_SEL_TGT_CROP_BOUNDS:
//      case V4L2_SEL_TGT_COMPOSE:
      // case V4L2_SEL_TGT_COMPOSE_DEFAULT:
      // case V4L2_SEL_TGT_COMPOSE_BOUNDS:
      // case V4L2_SEL_TGT_COMPOSE_PADDED:
         sel->r.top = 0;
         sel->r.left = 0;
         sel->r.width = dione_ir->fmt.width;
         sel->r.height = dione_ir->fmt.height;

         return 0;
   }

   return -EINVAL;
}

static int dione_ir_set_stream(struct v4l2_subdev *sd, int enable)
{
   struct dione_ir *priv = to_dione_ir(sd);
	struct device *dev = &priv->tc35_client->dev;
   int err;
   u32 bleVal;

   if (enable)
   {
      struct regmap *ctl_regmap = priv->ctl_regmap;

		dione_ir_set_mode(priv);

      err = regmap_write(ctl_regmap, PP_MISC, 0);
      if (!err)
         err = regmap_update_bits(ctl_regmap, CONFCTL,
                   CONFCTL_PPEN_MASK, CONFCTL_PPEN_MASK);

      if (err)
         dev_err(dev, "%s return code (%d)\n", __func__, err);
   }
   else
   {
      struct regmap *ctl_regmap = priv->ctl_regmap;
      struct regmap *tx_regmap = priv->tx_regmap;
      int err;

      err = regmap_update_bits(ctl_regmap, PP_MISC, PP_MISC_FRMSTOP_MASK,
                PP_MISC_FRMSTOP_MASK);
      if (!err)
         err = regmap_update_bits(ctl_regmap, CONFCTL,
                   CONFCTL_PPEN_MASK, 0);

      if (!err)
         err = regmap_update_bits(ctl_regmap, PP_MISC,
                   PP_MISC_RSTPTR_MASK,
                   PP_MISC_RSTPTR_MASK);

      if (!err)
      {
         dione_ir_regmap_format_32_ble((void *)&bleVal, CSIRESET_RESET_CNF_MASK | CSIRESET_RESET_MODULE_MASK);
         err = regmap_write(tx_regmap, CSIRESET, bleVal);
      }
      if (!err)
      {
         err = regmap_write(ctl_regmap, DBG_ACT_LINE_CNT, 0);
      }

      if (err)
         dev_err(dev, "%s return code (%d)\n", __func__, err);
   }

   return err;
}

static const struct v4l2_subdev_core_ops dione_ir_core_ops = {
   .subscribe_event = v4l2_ctrl_subdev_subscribe_event,
   .unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops dione_ir_video_ops = {
   .s_stream = dione_ir_set_stream,
};

static const struct v4l2_subdev_pad_ops dione_ir_pad_ops = {
   .enum_mbus_code = dione_ir_enum_mbus_code,
   .get_fmt = dione_ir_get_pad_format,
   .set_fmt = dione_ir_set_pad_format,
   .get_selection = dione_ir_get_selection,
   .enum_frame_size = dione_ir_enum_frame_size,
};

static const struct v4l2_subdev_ops sensor_subdev_ops = {
   .core = &dione_ir_core_ops,
   .video = &dione_ir_video_ops,
   .pad = &dione_ir_pad_ops,
};

static const struct v4l2_subdev_internal_ops dione_ir_internal_ops = {
   .open = dione_ir_open,
};

/* Initialize control handlers */
static int dione_ir_init_controls(struct dione_ir *dione_ir)
{
	struct device *dev = &dione_ir->tc35_client->dev;
   struct v4l2_ctrl_handler *ctrl_hdlr;
   struct v4l2_ctrl *ctrl;
	struct v4l2_fwnode_device_properties props;
   int ret;
   int hblank;
   int i;
	struct tc358746_input input;
	struct tc358746 params;

   ctrl_hdlr = &dione_ir->ctrl_handler;
   ret = v4l2_ctrl_handler_init(ctrl_hdlr, 16);
   if (ret)
      return ret;

   mutex_init(&dione_ir->mutex);
   ctrl_hdlr->lock = &dione_ir->mutex;

	input.mbus_fmt = dione_ir_mbus_codes[dione_ir->mbus_code_index]; // TODO support multpible codes
	input.refclk = dione_ir->def_clk_freq;
	input.num_lanes = dione_ir_ep_cfg.bus.mipi_csi2.num_data_lanes;
   input.discontinuous_clk = dione_ir_ep_cfg.bus.mipi_csi2.flags & V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK ? 1 : 0;
	input.pclk = dione_ir_supported_modes[dione_ir->mode].pix_clk_hz;
	input.width = dione_ir_supported_modes[dione_ir->mode].width;
	input.hblank = dione_ir_supported_modes[dione_ir->mode].line_length - dione_ir_supported_modes[dione_ir->mode].width;

	for (i = 0; i < dione_ir_ep_cfg.nr_of_link_frequencies; i++) {
		input.link_frequency = dione_ir_ep_cfg.link_frequencies[i];
      link_freq_menu_items[0] = input.link_frequency;
		if (tc358746_calculate(&params, &input) == 0)
			break;
	}

	if (i >= dione_ir_ep_cfg.nr_of_link_frequencies) {
		dev_err(dev, "could not calculate parameters for tc358746\n");
		return -EINVAL;
	}

   dev_err(dev, "Link frequency = %lld\n", link_freq_menu_items[0]);
   ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr, &sensor_ctrl_ops, V4L2_CID_LINK_FREQ,
                                0, 0, link_freq_menu_items);
   // v4l2_ctrl_new_std(ctrl_hdlr, &sensor_ctrl_ops, V4L2_CID_LINK_FREQ,
         // input.link_frequency, input.link_frequency, 1, input.link_frequency);

   if (ctrl)
          ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;


   /* By default, PIXEL_RATE is read only */
   v4l2_ctrl_new_std(ctrl_hdlr, &sensor_ctrl_ops, V4L2_CID_PIXEL_RATE,
         20000000, 20000000, 1, 20000000);

   /* Initial vblank/hblank/exposure parameters based on current mode */
   ctrl = v4l2_ctrl_new_std(ctrl_hdlr, &sensor_ctrl_ops, V4L2_CID_VBLANK,
         0, 0, 1, 0);
   if (ctrl)
      ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

   hblank = dione_ir_supported_modes[dione_ir->mode].line_length - dione_ir_supported_modes[dione_ir->mode].width;
   dione_ir->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &sensor_ctrl_ops,
                                     V4L2_CID_HBLANK, hblank, hblank,
                                     1, hblank);
   if (dione_ir->hblank)
      dione_ir->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

   ctrl = v4l2_ctrl_new_std(ctrl_hdlr, &sensor_ctrl_ops, V4L2_CID_EXPOSURE,
         1, 10, 1, 1);
   if (ctrl)
      ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrl_hdlr, &sensor_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  1, 10,
			  1, 1);

   if (ctrl_hdlr->error) {
      ret = ctrl_hdlr->error;
      dev_err(dev, "%s control init failed (%d)\n",
            __func__, ret);
      goto error;
   }

	ret = v4l2_fwnode_device_parse(dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &sensor_ctrl_ops,
					      &props);
	if (ret)
		goto error;

   dione_ir->sd.ctrl_handler = ctrl_hdlr;

   return 0;

error:
   v4l2_ctrl_handler_free(ctrl_hdlr);
   mutex_destroy(&dione_ir->mutex);

   return ret;
}

static void dione_ir_free_controls(struct dione_ir *dione_ir)
{
   v4l2_ctrl_handler_free(dione_ir->sd.ctrl_handler);
   mutex_destroy(&dione_ir->mutex);
}

static int dione_ir_check_hwcfg(struct device *dev)
{
   struct fwnode_handle *endpoint;

   endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
   if (!endpoint) {
      dev_err(dev, "endpoint node not found\n");
      return -EINVAL;
   }

   if (v4l2_fwnode_endpoint_alloc_parse(endpoint, &dione_ir_ep_cfg)) {
      dev_err(dev, "could not parse endpoint\n");
      goto error_out;
   }

   return 0;

error_out:
   v4l2_fwnode_endpoint_free(&dione_ir_ep_cfg);
   fwnode_handle_put(endpoint);
   return -EINVAL;
}

static int dione_ir_probe_sensor(struct dione_ir *priv)
{
	struct device *dev = &priv->tc35_client->dev;
	int err = 0;

	priv->quick_mode = quick_mode;
   priv->tx_regmap = devm_regmap_init_i2c(priv->tc35_client,
                      &tx_regmap_config);

   if (IS_ERR(priv->tx_regmap)) {
      dev_err(dev, "tx_regmap init failed: %ld\n",
            PTR_ERR(priv->tx_regmap));
      err = -ENODEV;
   }

   priv->ctl_regmap = devm_regmap_init_i2c(priv->tc35_client,
                      &ctl_regmap_config);

   if (IS_ERR(priv->ctl_regmap)) {
      dev_err(dev, "ctl_regmap init failed: %ld\n",
            PTR_ERR(priv->ctl_regmap));
      err = -ENODEV;
   }

	if (!err) {
		err = dione_ir_board_setup(priv);
		if (err && !priv->tc35_found && !priv->fpga_found) {
			dev_err(dev, "tc35 not found and fpga not found\n");
			err = dione_ir_board_setup(priv);
		}

		if (err && priv->tc35_found && priv->fpga_found)
      {
			dev_err(dev, "dione_ir_board_setup error: %d\n", err);
         return err;
      }
	}

	if (!test_mode && priv->fpga_client != NULL) {
		i2c_unregister_device(priv->fpga_client);
		priv->fpga_client = NULL;
	}

	return err;
}

static int dione_ir_parse_fpga_address(struct i2c_client *client,
				       struct dione_ir *priv)
{
	struct device_node *node = client->dev.of_node;
	int len;

	if (!of_get_property(node, "fpga-address", &len)) {
		dev_err(&client->dev,
			"fpga-address property not found or too many\n");
		return -EINVAL;
	}

	priv->fpga_address = devm_kzalloc(&client->dev, len, GFP_KERNEL);
	if (!priv->fpga_address)
		return -ENOMEM;

	priv->fpga_address_num = len / sizeof(*priv->fpga_address);

	return of_property_read_u32_array(node, "fpga-address",
					  priv->fpga_address,
					  priv->fpga_address_num);
}

static int dione_ir_probe(struct i2c_client *client)
{
   struct device *dev = &client->dev;
   struct dione_ir *dione_ir;
   int ret;
   int i;
   int err;

   dione_ir = devm_kzalloc(dev, sizeof(*dione_ir), GFP_KERNEL);
   if (!dione_ir)
      return -ENOMEM;

   /* Check the hardware configuration in device tree */
   if (dione_ir_check_hwcfg(dev))
      return -EINVAL;

   dione_ir->clk = devm_clk_get_optional(&client->dev, NULL);
   if (IS_ERR(dione_ir->clk))
          return dev_err_probe(&client->dev, PTR_ERR(dione_ir->clk),
                               "error getting clock\n");
   if (!dione_ir->clk) {
          dev_info(&client->dev,
                  "no clock provided, using clock-frequency property\n");

          device_property_read_u32(&client->dev, "clock-frequency", &dione_ir->def_clk_freq);
   } else if (IS_ERR(dione_ir->clk)) {
          return dev_err_probe(&client->dev, PTR_ERR(dione_ir->clk),
                               "error getting clock\n");
   } else {
          dione_ir->def_clk_freq = clk_get_rate(dione_ir->clk);
   }

	err = dione_ir_parse_fpga_address(client, dione_ir);
	if (err < 0)
		return err;

	dione_ir->tc35_client = client;
	if (test_mode)
		quick_mode = 1;

	err = dione_ir_probe_sensor(dione_ir);
	if (err < 0)
   {
      dev_err(dev, "dione_ir_probe_sensor error: %d\n", err);
		return err;
   }

   v4l2_subdev_init(&dione_ir->sd, &sensor_subdev_ops);
   /* the owner is the same as the i2c_client's driver owner */
   dione_ir->sd.owner = dev->driver->owner;
   dione_ir->sd.dev =dev;
   v4l2_set_subdevdata(&dione_ir->sd, client);

   /* initialize name */
   snprintf(dione_ir->sd.name, sizeof(dione_ir->sd.name), "%s",
         dev->driver->name);

   dione_ir->fmt.width = dione_ir_supported_modes[0].width;
   dione_ir->fmt.height = dione_ir_supported_modes[0].height;
   dione_ir->fmt.code = dione_ir_mbus_codes[0];
   dione_ir->fmt.field = V4L2_FIELD_NONE;
  	dione_ir->fmt.colorspace = V4L2_COLORSPACE_SRGB;
   // dione_ir->fmt.ycbcr_enc =
      // V4L2_MAP_YCBCR_ENC_DEFAULT(dione_ir->fmt.colorspace);
   // dione_ir->fmt.quantization =
      // V4L2_MAP_QUANTIZATION_DEFAULT(true,
            // dione_ir->fmt.colorspace,
            // dione_ir->fmt.ycbcr_enc);
   // dione_ir->fmt.xfer_func =
      // V4L2_MAP_XFER_FUNC_DEFAULT(dione_ir->fmt.colorspace);

   ret = dione_ir_init_controls(dione_ir);
   if (ret)
      return ret;

   /* Initialize subdev */
   dione_ir->sd.internal_ops = &dione_ir_internal_ops;
   dione_ir->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
      V4L2_SUBDEV_FL_HAS_EVENTS;
   dione_ir->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

   /* Initialize source pads */
   dione_ir->pad.flags = MEDIA_PAD_FL_SOURCE;

   ret = media_entity_pads_init(&dione_ir->sd.entity, 1,
         &dione_ir->pad);
   if (ret) {
      dev_err(dev, "failed to init entity pads: %d\n", ret);
      goto error_handler_free;
   }

   ret = v4l2_async_register_subdev_sensor(&dione_ir->sd);
   if (ret < 0) {
      dev_err(dev, "failed to register dione_ir sub-device: %d\n", ret);
      goto error_media_entity;
   }

   dev_info(dev, "registered\n");

   return 0;

error_media_entity:
   media_entity_cleanup(&dione_ir->sd.entity);

error_handler_free:
   dione_ir_free_controls(dione_ir);

   device_destroy(i2c_clients[i].pClass_chnod, i2c_clients[i].chnod_device_number);
   class_destroy(i2c_clients[i].pClass_chnod);
   unregister_chrdev(i2c_clients[i].chnod_major_number, i2c_clients[i].chnod_name);
   i2c_clients[i].chnod_name[0] = 0;

   return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,1,1)
static int dione_ir_remove(struct i2c_client *client)
#else
static void dione_ir_remove(struct i2c_client *client)
#endif
{
   struct v4l2_subdev *sd = i2c_get_clientdata(client);
   struct device *dev = &client->dev;
   struct dione_ir *dione_ir = to_dione_ir(sd);
   int i;

   v4l2_async_unregister_subdev(&dione_ir->sd);
   media_entity_cleanup(&dione_ir->sd.entity);
   dione_ir_free_controls(dione_ir);

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

static const struct of_device_id dione_ir_id[] = {
   { .compatible = "xenics,dioneir" },
   { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dione_ir_id);

static struct i2c_driver dione_ir_i2c_driver = {
#if LINUX_VERSION_CODE <= KERNEL_VERSION(6,2,0)
   .probe_new = dione_ir_probe,
#else   
   .probe = dione_ir_probe,
#endif
   .remove = dione_ir_remove,
   .driver = {
      .name = "dioneir",
      .of_match_table	= dione_ir_id,
   },
};

module_i2c_driver(dione_ir_i2c_driver);

MODULE_DESCRIPTION("Media Controller driver for Xenics Dione IR sensors");
MODULE_AUTHOR("Xenics Exosens");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
