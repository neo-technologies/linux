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

#ifndef __ROCKCHIP_LCDC_DRV_H__
#define __ROCKCHIP_LCDC_DRV_H__

#include <drm/drmP.h>

#define ROCKCHIP_LCDC_MAX_HEIGHT	2048
#define ROCKCHIP_LCDC_MAX_WIDTH		2048

#define RK3066_LCDC			1
#define RK3188_LCDC			2

struct rockchip_drm_priv {
	void __iomem *mmio;

	struct clk *clk;	/* functional clock */
	struct clk *disp_clk;	/* display clock */

	int rev;		/* LCDC revision */

	struct drm_fbdev_cma *fbdev;

	struct drm_crtc *crtc;

	struct drm_gem_cma_object *cursor;

	/*
	 * LCDC_SYS_CTRL register needs a delay to read back values after write
	 * so we must use a temporary backup register for bit clearing/setting
	 */
	uint32_t sys_bak;
};

#define DBG(fmt, ...) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)

struct drm_crtc *rockchip_crtc_create(struct drm_device *dev);
irqreturn_t rockchip_crtc_irq(struct drm_crtc *crtc);
void rockchip_crtc_update_clk(struct drm_crtc *crtc);

#endif /* __ROCKCHIP_LCDC_DRV_H__ */
