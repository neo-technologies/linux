/*
 * Copyright (C) 2014 NEO-Technologies
 * Author: Julien Chauveau <julien.chauveau@neo-technologies.fr>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ROCKCHIP_LCDC_REGS_H__
#define __ROCKCHIP_LCDC_REGS_H__

#include <linux/bitops.h>

#include "rockchip_lcdc_drv.h"

/* Rockchip RK3188 LCDC Registers */
#define LCDC_SYS_CTRL			0x00
#define LCDC_DSP_CTRL0			0x04
#define LCDC_DSP_CTRL1			0x08
#define LCDC_MCU_CTRL			0x0c
#define LCDC_INT_STATUS			0x10
#define LCDC_ALPHA_CTRL			0x14
#define LCDC_WIN0_COLOR_KEY		0x18
#define LCDC_WIN1_COLOR_KEY		0x1c
#define LCDC_WIN0_YRGB_MST0		0x20
#define LCDC_WIN0_CBR_MST0		0x24
#define LCDC_WIN0_YRGB_MST1		0x28
#define LCDC_WIN0_CBR_MST1		0x2c
#define LCDC_WIN_VIR			0x30
#define LCDC_WIN0_ACT_INFO		0x34
#define LCDC_WIN0_DSP_INFO		0x38
#define LCDC_WIN0_DSP_ST		0x3c
#define LCDC_WIN0_SCL_FACTOR_YRGB	0x40
#define LCDC_WIN0_SCL_FACTOR_CBR	0x44
#define LCDC_WIN0_SCL_OFFSET		0x48
#define LCDC_WIN1_MST			0x4c
#define LCDC_WIN1_DSP_INFO		0x50
#define LCDC_WIN1_DSP_ST		0x54
#define LCDC_HWC_MST			0x58
#define LCDC_HWC_DSP_ST			0x5c
#define LCDC_HWC_COLOR_LUT0		0x60
#define LCDC_HWC_COLOR_LUT1		0x64
#define LCDC_HWC_COLOR_LUT2		0x68
#define LCDC_DSP_HTOTAL_HS_END		0x6c
#define LCDC_DSP_HACT_ST_END		0x70
#define LCDC_DSP_VTOTAL_VS_END		0x74
#define LCDC_DSP_VACT_ST_END		0x78
#define LCDC_DSP_VS_ST_END_F1		0x7c
#define LCDC_DSP_VACT_ST_END_F1		0x80
#define LCDC_REG_CFG_DONE		0x90
#define LCDC_MCU_BYPASS_WPORT		0x100
#define LCDC_MCU_BYPASS_RPORT		0x200
#define LCDC_WIN1_LUT_ADDR		0x400
#define LCDC_DSP_DUT_ADDR		0x800

/* LCDC_SYS_CTRL */
#define AUTO_GATING_EN			BIT(31)
#define LCDC_STANDBY			BIT(30)
#define WIN1_LUT_EN			BIT(27)
#define HWC_LOAD_EN			BIT(26)
#define LCDC_DMA_BURST_SIZE(x)		((x) << 24)
# define LCDC_DMA_BURST_16		LCDC_DMA_BURST_SIZE(0)
# define LCDC_DMA_BURST_8		LCDC_DMA_BURST_SIZE(1)
# define LCDC_DMA_BURST_4		LCDC_DMA_BURST_SIZE(2)
# define LCDC_DMA_BURST_MASK		LCDC_DMA_BURST_SIZE(3)
#define HWC_SIZE_64			BIT(10)
#define HWC_MODE_REVERSED		BIT(9)
#define WIN1_DATA_FMT(x)		((x) << 6)	 /* BIT(6..8) */
# define WIN1_DATA_ARGB888		WIN1_DATA_FMT(0)
# define WIN1_DATA_RGB888		WIN1_DATA_FMT(1)
# define WIN1_DATA_RGB565		WIN1_DATA_FMT(2)
# define WIN1_DATA_MASK			WIN1_DATA_FMT(7)
#define WIN0_DATA_FMT(x)		((x) << 3)	 /* BIT(3..5) */
# define WIN0_DATA_ARGB888		WIN0_DATA_FMT(0)
# define WIN0_DATA_RGB888		WIN0_DATA_FMT(1)
# define WIN0_DATA_RGB565		WIN0_DATA_FMT(2)
# define WIN0_DATA_MASK			WIN0_DATA_FMT(7)
#define HWC_EN				BIT(2)
#define WIN1_EN				BIT(1)
#define WIN0_EN				BIT(0)

/* LCDC_DSP_CTRL0 */
#define DSP_DCLK_INVERT			BIT(7)
#define DSP_DEN_NEGATIVE		BIT(6)
#define DSP_VSYNC_POSITIVE		BIT(5)
#define DSP_HSYNC_POSITIVE		BIT(4)

/* LCDC_DSP_CTRL0 */
#define DSP_BLANK_EN			BIT(24)

/* LCDC_INT_STATUS */
#define DSP_LINE_FRAG_NUM(x)		((x & 0xfff) << 12)
#define LCDC_INT_MASK(x)		(x & 0xf)
#define LCDC_INT_CLEAR(x)		(LCDC_INT_MASK(x) << 8)
#define LCDC_INT_EN(x)			(LCDC_INT_MASK(x) << 4)
#define BE_INT				BIT(3)
#define LF_INT				BIT(2)
#define FS_INT				BIT(1)
#define HS_INT				BIT(0)

/* LCDC_WIN_VIR */
#define WIN1_VIR_STRIDE(x)		(((x) & 0xffff) << 16)
#define WIN0_VIR_STRIDE(x)		((x) & 0xffff)

/*
 * Register helper functions
 */

static inline void
rockchip_lcdc_write(struct drm_device *dev, uint32_t reg, uint32_t data)
{
	struct rockchip_drm_priv *priv = dev->dev_private;
	writel(data, priv->mmio + reg);
	DBG("%p: %#x", priv->mmio + reg, data);

	/* store backup value for sys_ctrl register */
	if (reg == LCDC_SYS_CTRL)
		priv->sys_bak = data & ~HWC_LOAD_EN; /* auto-cleared */
}

static inline uint32_t
rockchip_lcdc_read(struct drm_device *dev, uint32_t reg)
{
	struct rockchip_drm_priv *priv = dev->dev_private;
	DBG("%p: %#x", priv->mmio + reg, ioread32(priv->mmio + reg));

	/* return backup value for sys_ctrl register */
	if (reg == LCDC_SYS_CTRL) {
		DBG(" sys_bak: %#x", priv->sys_bak);
		return priv->sys_bak;
	}

	return readl(priv->mmio + reg);
}

static inline void
rockchip_lcdc_set(struct drm_device *dev, uint32_t reg, uint32_t mask)
{
	rockchip_lcdc_write(dev, reg, rockchip_lcdc_read(dev, reg) | mask);
}

static inline void
rockchip_lcdc_clear(struct drm_device *dev, uint32_t reg, uint32_t mask)
{
	rockchip_lcdc_write(dev, reg, rockchip_lcdc_read(dev, reg) & ~mask);
}

static inline uint32_t
rockchip_lcdc_read_irqstatus(struct drm_device *dev)
{
	return LCDC_INT_MASK(rockchip_lcdc_read(dev, LCDC_INT_STATUS));
}

static inline void
rockchip_lcdc_enable_irqstatus(struct drm_device *dev, uint32_t mask)
{
	rockchip_lcdc_set(dev, LCDC_INT_STATUS, LCDC_INT_EN(mask));
}

static inline void
rockchip_lcdc_disable_irqstatus(struct drm_device *dev, uint32_t mask)
{
	rockchip_lcdc_clear(dev, LCDC_INT_STATUS, LCDC_INT_EN(mask));
}

static inline void
rockchip_lcdc_clear_irqstatus(struct drm_device *dev, uint32_t mask)
{
	rockchip_lcdc_set(dev, LCDC_INT_STATUS, LCDC_INT_CLEAR(mask));
}

static inline void rockchip_lcdc_done(struct drm_device *dev)
{
	rockchip_lcdc_write(dev, LCDC_REG_CFG_DONE, 1);
}

#endif /* __ROCKCHIP_LCDC_REGS_H__ */
