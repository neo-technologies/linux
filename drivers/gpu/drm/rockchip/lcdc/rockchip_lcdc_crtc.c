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

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>

#include "rockchip_lcdc_drv.h"
#include "rockchip_lcdc_regs.h"

struct rockchip_crtc {
	struct drm_crtc base;
	dma_addr_t start;
	struct drm_pending_vblank_event *event;
	int dpms;
};

#define to_rockchip_crtc(x) container_of(x, struct rockchip_crtc, base)

static void set_scanout(struct drm_crtc *crtc)
{
	struct rockchip_crtc *rcrtc = to_rockchip_crtc(crtc);
	struct drm_device *drm = crtc->dev;
	struct device *dev = drm->dev;

	DBG();

	pm_runtime_get_sync(dev);

	rockchip_lcdc_write(drm, LCDC_WIN0_YRGB_MST0, rcrtc->start);
	rockchip_lcdc_write(drm, LCDC_WIN0_CBR_MST0, 0);
	rockchip_lcdc_write(drm, LCDC_WIN0_YRGB_MST1, 0);
	rockchip_lcdc_write(drm, LCDC_WIN0_CBR_MST1, 0);

	rockchip_lcdc_done(drm);

	pm_runtime_put_sync(dev);
}

static void update_scanout(struct drm_crtc *crtc)
{
	struct rockchip_crtc *rcrtc = to_rockchip_crtc(crtc);
	struct drm_framebuffer *fb = crtc->primary->fb;
	struct drm_gem_cma_object *gem;
	unsigned int depth, bpp;

	DBG();

	drm_fb_get_bpp_depth(fb->pixel_format, &depth, &bpp);
	gem = drm_fb_cma_get_gem_obj(fb, 0);

	rcrtc->start = gem->paddr + fb->offsets[0] +
			(crtc->y * fb->pitches[0]) + (crtc->x * bpp/8);

	if (rcrtc->dpms == DRM_MODE_DPMS_ON) {
		/*
		 * already enabled, so just mark the frames that need
		 * updating and they will be updated on vblank
		 */
		drm_crtc_vblank_get(crtc);
	} else {
		/* not enabled yet, so update registers immediately */
		set_scanout(crtc);
	}
}

static void start(struct drm_crtc *crtc)
{
	DBG();

	rockchip_lcdc_clear(crtc->dev, LCDC_SYS_CTRL, LCDC_STANDBY);
}

static void stop(struct drm_crtc *crtc)
{
	DBG();

	rockchip_lcdc_set(crtc->dev, LCDC_SYS_CTRL, LCDC_STANDBY);
}


/*
 * DRM CRTC helper functions
 */

static void rockchip_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct rockchip_crtc *rcrtc = to_rockchip_crtc(crtc);
	struct drm_device *drm = crtc->dev;
	struct device *dev = drm->dev;

	DBG("%d", mode);

	/* we only care about ON or OFF */
	if (mode != DRM_MODE_DPMS_ON)
		mode = DRM_MODE_DPMS_OFF;

	if (rcrtc->dpms == mode)
		return;

	rcrtc->dpms = mode;

	pm_runtime_get_sync(dev);

	if (mode == DRM_MODE_DPMS_ON) {
		pm_runtime_forbid(dev);
		start(crtc);
	} else {
		stop(crtc);
		pm_runtime_allow(dev);
	}

	pm_runtime_put_sync(dev);
}

static void rockchip_crtc_prepare(struct drm_crtc *crtc)
{
	rockchip_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void rockchip_crtc_commit(struct drm_crtc *crtc)
{
	rockchip_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static bool rockchip_crtc_mode_fixup(struct drm_crtc *crtc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	DBG();

	/* TODO: fixup modes */

	return true;
}

static int
rockchip_crtc_mode_valid(struct drm_crtc *crtc, struct drm_display_mode *mode)
{
	uint32_t hbp, hfp, hsw, vbp, vfp, vsw;

	/* width must be within the range that the LCD Controller supports */
	if (mode->hdisplay > ROCKCHIP_LCDC_MAX_WIDTH)
		return MODE_VIRTUAL_X;

	/* width must be multiple of 16 */
	if (mode->hdisplay & 0xf)
		return MODE_VIRTUAL_X;

	if (mode->vdisplay > ROCKCHIP_LCDC_MAX_HEIGHT)
		return MODE_VIRTUAL_Y;

	DBG("Processing mode %dx%d@%d with pixel clock %d",
		mode->hdisplay, mode->vdisplay,
		drm_mode_vrefresh(mode), mode->clock);

	hbp = mode->htotal - mode->hsync_end;
	hfp = mode->hsync_start - mode->hdisplay;
	hsw = mode->hsync_end - mode->hsync_start;
	vbp = mode->vtotal - mode->vsync_end;
	vfp = mode->vsync_start - mode->vdisplay;
	vsw = mode->vsync_end - mode->vsync_start;

	if ((hbp-1) & ~0x3ff) {
		DBG("Pruning mode: Horizontal Back Porch out of range");
		return MODE_HBLANK_WIDE;
	}

	if ((hfp-1) & ~0x3ff) {
		DBG("Pruning mode: Horizontal Front Porch out of range");
		return MODE_HBLANK_WIDE;
	}

	if ((hsw-1) & ~0x3ff) {
		DBG("Pruning mode: Horizontal Sync Width out of range");
		return MODE_HSYNC_WIDE;
	}

	if (vbp & ~0xff) {
		DBG("Pruning mode: Vertical Back Porch out of range");
		return MODE_VBLANK_WIDE;
	}

	if (vfp & ~0xff) {
		DBG("Pruning mode: Vertical Front Porch out of range");
		return MODE_VBLANK_WIDE;
	}

	if ((vsw-1) & ~0x3f) {
		DBG("Pruning mode: Vertical Sync Width out of range");
		return MODE_VSYNC_WIDE;
	}

	return MODE_OK;
}

static int rockchip_crtc_mode_set(struct drm_crtc *crtc,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode,
		int x, int y,
		struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct drm_framebuffer *win0 = crtc->primary->fb;
	uint32_t reg;
	uint32_t hbp, hfp, hsw, vbp, vfp, vsw;
	unsigned int depth, bpp;
	int ret;

	DBG("x,y: %d,%d", x, y);

	ret = rockchip_crtc_mode_valid(crtc, mode);
	if (WARN_ON(ret))
		return ret;

	pm_runtime_get_sync(dev->dev);

	/* LCDC_SYS_CTRL */
	/* FIXME */
//	reg = rockchip_lcdc_read(dev, LCDC_SYS_CTRL);
	reg = 0;
	reg |= AUTO_GATING_EN | WIN0_EN;

	/* Configure display type: */
	drm_fb_get_bpp_depth(win0->pixel_format, &depth, &bpp);
	switch (win0->pixel_format) {
	case DRM_FORMAT_RGB565:
		reg |= WIN0_DATA_RGB565;
		break;
	case DRM_FORMAT_RGB888:
		reg |= WIN0_DATA_RGB888;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		reg |= WIN0_DATA_ARGB888;
		break;
	default:
		dev_err(dev->dev, "invalid pixel format\n");
		return -EINVAL;
	}

	rockchip_lcdc_write(dev, LCDC_SYS_CTRL, reg);

	/* LCDC_DSP_CTRL0 */
	reg = 0;
	/* FIXME check flag for pixelclk-active ??? */
//	if (adjusted_mode->flags->DRM_MODE_FLAG_NDCLK)
		reg |= DSP_DCLK_INVERT;

	if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
		reg |= DSP_HSYNC_POSITIVE;

	if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
		reg |= DSP_VSYNC_POSITIVE;

	rockchip_lcdc_write(dev, LCDC_DSP_CTRL0, reg);

	/* LCDC_DSP_CTRL1 */
	/* FIXME */
	rockchip_lcdc_write(dev, LCDC_DSP_CTRL1, 0);

	/* LCDC_WIN_VIR */
	reg = WIN1_VIR_STRIDE(0) | WIN0_VIR_STRIDE(win0->width);
	rockchip_lcdc_write(dev, LCDC_WIN_VIR, reg);

	/* LCDC_WIN0_ACT_INFO */
	reg = (win0->height - 1) << 16 | (win0->width - 1);
	rockchip_lcdc_write(dev, LCDC_WIN0_ACT_INFO, reg);

	/* LCDC_WIN0_DSP_INFO */
	rockchip_lcdc_write(dev, LCDC_WIN0_DSP_INFO, reg);

	/* Configure timings: */
	hbp = mode->htotal - mode->hsync_end;
	hfp = mode->hsync_start - mode->hdisplay;
	hsw = mode->hsync_end - mode->hsync_start;
	vbp = mode->vtotal - mode->vsync_end;
	vfp = mode->vsync_start - mode->vdisplay;
	vsw = mode->vsync_end - mode->vsync_start;

	drm_mode_debug_printmodeline(mode);
	drm_mode_debug_printmodeline(adjusted_mode);
	DBG("%dx%d, hbp=%u, hfp=%u, hsw=%u, vbp=%u, vfp=%u, vsw=%u",
			mode->hdisplay, mode->vdisplay, hbp, hfp, hsw, vbp, vfp, vsw);

	/* LCDC_WIN0_DSP_ST */
	reg = vfp << 16 | hfp;
	rockchip_lcdc_write(dev, LCDC_WIN0_DSP_ST, reg);

	rockchip_lcdc_write(dev, LCDC_WIN0_SCL_FACTOR_YRGB, 0x10001000);
	rockchip_lcdc_write(dev, LCDC_WIN0_SCL_FACTOR_CBR, 0x10001000);
	rockchip_lcdc_write(dev, LCDC_WIN0_SCL_OFFSET, 0);

	/* LCDC_WIN1_DSP_INFO */
	rockchip_lcdc_write(dev, LCDC_WIN1_DSP_INFO, 0);
	rockchip_lcdc_write(dev, LCDC_WIN1_DSP_ST, 0);

	/* LCDC_DSP_HTOTAL_HS_END */
	reg = mode->htotal << 16 | hsw;
	rockchip_lcdc_write(dev, LCDC_DSP_HTOTAL_HS_END, reg);

	/* LCDC_DSP_HACT_ST_END */
	reg = hfp << 16 | mode->hsync_start;
	rockchip_lcdc_write(dev, LCDC_DSP_HACT_ST_END, reg);

	/* LCDC_DSP_VTOTAL_VS_END */
	reg = mode->vtotal << 16 | vsw;
	rockchip_lcdc_write(dev, LCDC_DSP_VTOTAL_VS_END, reg);

	/* LCDC_DSP_VACT_ST_END */
	reg = vfp << 16 | mode->vsync_start;
	rockchip_lcdc_write(dev, LCDC_DSP_VACT_ST_END, reg);

	update_scanout(crtc);
	rockchip_crtc_update_clk(crtc);

	pm_runtime_put_sync(dev->dev);

	return 0;
}

static int rockchip_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
		struct drm_framebuffer *old_fb)
{
	DBG("x,y: %d,%d", x, y);
	update_scanout(crtc);
	return 0;
}

static const struct drm_crtc_helper_funcs rockchip_crtc_helper_funcs = {
	.dpms           = rockchip_crtc_dpms,
	.prepare        = rockchip_crtc_prepare,
	.commit         = rockchip_crtc_commit,
	.mode_fixup     = rockchip_crtc_mode_fixup,
	.mode_set       = rockchip_crtc_mode_set,
	.mode_set_base  = rockchip_crtc_mode_set_base,
};

void rockchip_crtc_update_clk(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct rockchip_drm_priv *priv = dev->dev_private;
	unsigned int rate = crtc->mode.crtc_clock * 1000;
	int ret;

	pm_runtime_get_sync(dev->dev);

	rockchip_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);

	ret = clk_set_rate(priv->disp_clk, rate);
	if (ret) {
		dev_err(dev->dev, "failed to set display clock rate to %d"
			" (error %d)\n", rate, ret);
		goto out;
	}

	DBG("pixel clock rate set to %lu (requested %d)",
		clk_get_rate(priv->disp_clk), rate);

	rockchip_crtc_dpms(crtc, DRM_MODE_DPMS_ON);

out:
	pm_runtime_put_sync(dev->dev);
}

irqreturn_t rockchip_crtc_irq(struct drm_crtc *crtc)
{
	struct drm_device *drm = crtc->dev;
//	struct rockchip_crtc *rcrtc = to_rockchip_crtc(crtc);

	uint32_t stat = rockchip_lcdc_read_irqstatus(drm);
	rockchip_lcdc_clear_irqstatus(drm, stat);

	/* FIXME: debug only (disable after 5 irq) */
/*
	if (drm->vblank[0].count.counter > 4)
		rockchip_lcdc_disable_irqstatus(drm, stat);

	DBG("vblank count %d", drm->vblank[0].count.counter);
	DBG("vblank refcount %d", drm->vblank[0].refcount.counter);
*/
	if (stat & BE_INT) {
		/* FIXME */
		stop(crtc);
		dev_err(drm->dev, "error: %#x\n", stat);
		start(crtc);
		return IRQ_HANDLED;
	}

	if (stat & FS_INT) {
/* FIXME
		struct drm_pending_vblank_event *event;
		unsigned long flags;
*/
		int index = drm_crtc_index(crtc);

		set_scanout(crtc);

		drm_handle_vblank(drm, index);
		drm_crtc_vblank_put(crtc);

/* TODO: vblank & page flip

		spin_lock_irqsave(&drm->event_lock, flags);
		event = rcrtc->event;
		rcrtc->event = NULL;
		if (event)
			drm_send_vblank_event(drm, index, event);
		spin_unlock_irqrestore(&drm->event_lock, flags);
*/
		return IRQ_HANDLED;
	}

	DRM_ERROR("Unknown IRQs: %#02x\n", stat);
	return IRQ_NONE;
}


/*
 * DRM Cursor plane functions
 */

/*
static const struct drm_plane_funcs rockchip_cursor_funcs = {
	.update_plane	= rockchip_cursor_update_plane,
	.disable_plane	= rockchip_cursor_disable_plane,
};
*/

/*
 * DRM CRTC functions
 */

static int rockchip_crtc_cursor_set(struct drm_crtc *crtc,
	struct drm_file *file_priv, uint32_t handle,
	uint32_t width, uint32_t height)
{
	struct drm_device *dev = crtc->dev;
	struct rockchip_drm_priv *priv = dev->dev_private;
	struct drm_gem_object *cursor;
	struct drm_gem_cma_object *cursor_in, *cursor_out;
	uint32_t *cursor_in_buf;
	uint64_t *cursor_out_buf, blk;
	int i;

	if (!handle) {
		/* disable cursor */
		rockchip_lcdc_clear(dev, LCDC_SYS_CTRL, HWC_EN);
		goto done;
		return 0;
	}

	if (width == 64 && height == 64) {
		rockchip_lcdc_set(dev, LCDC_SYS_CTRL, HWC_SIZE_64);
	} else if (width == 32 && height == 32) {
		rockchip_lcdc_clear(dev, LCDC_SYS_CTRL, HWC_SIZE_64);
	} else {
		return -EINVAL;
	}

	/* get input cursor object */
	cursor = drm_gem_object_lookup(dev, file_priv, handle);
	if (!cursor)
		return -ENOENT;

	DBG("cursor size: %dx%d (%u bytes)", width, height, cursor->size);

	cursor_in = to_drm_gem_cma_obj(cursor);

	/* free previous cursor buffer */
	if (priv->cursor)
		drm_gem_cma_free_object(&priv->cursor->base);

	/* create cursor buffer for scanout */
	cursor_out = drm_gem_cma_create(dev, 64 * 16); /* 64x64 x 2bits */
	if (IS_ERR(cursor_out)) {
		dev_err(dev->dev, "failed to allocate cursor memory\n");
		drm_gem_object_unreference_unlocked(cursor);
		return -ENOMEM;
	}
	priv->cursor = cursor_out;

	/* convert cursor image from ARGB to 2bpp */
	cursor_in_buf = cursor_in->vaddr;
	cursor_out_buf = cursor_out->vaddr;

	cursor_in_buf += width - 1;
	for (i = 0; i < cursor->size / 4; i++) {
		blk <<= 2;
		if (*cursor_in_buf < 0xff000000)	/* transparent */
			blk |= 3;
		else if (*cursor_in_buf >= 0xffbbbbbb)	/* white */
			blk |= 2;
		else if (*cursor_in_buf >= 0xff555555)	/* gray */
			blk |= 1;
		cursor_in_buf--;

		if (i % width == width - 1) {
			cursor_out_buf[0] = blk;
			cursor_out_buf += 2;
			cursor_in_buf += 2 * width;
		} else if (i % (width / 2) == width / 2 - 1) {
			cursor_out_buf[1] = blk;
		}
	}

	drm_gem_object_unreference_unlocked(cursor);

	/* set cursor framebuffer */
	rockchip_lcdc_write(dev, LCDC_HWC_MST, cursor_out->paddr);

	/* use normal color mode */
	rockchip_lcdc_clear(dev, LCDC_SYS_CTRL, HWC_MODE_REVERSED);

	/* set color palette */
	rockchip_lcdc_write(dev, LCDC_HWC_COLOR_LUT0, 0);	  /* black */
	rockchip_lcdc_write(dev, LCDC_HWC_COLOR_LUT1, 0x808080);  /* gray */
	rockchip_lcdc_write(dev, LCDC_HWC_COLOR_LUT2, 0xffffff);  /* white */

	/* enable cursor */
	rockchip_lcdc_set(dev, LCDC_SYS_CTRL, HWC_EN | HWC_LOAD_EN);

done:
	rockchip_lcdc_done(dev);

	return 0;
}

static int rockchip_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct drm_device *dev = crtc->dev;

	uint32_t reg = rockchip_lcdc_read(dev, LCDC_WIN0_DSP_ST);

	reg += y << 16 | x;
	rockchip_lcdc_write(dev, LCDC_HWC_DSP_ST, reg);

	rockchip_lcdc_done(dev);

	return 0;
}

static int rockchip_crtc_set_config(struct drm_mode_set *set)
{
	/* TODO: change crtc according to framebuffer depth */

	if (set->crtc && set->crtc->primary && set->crtc->primary->fb)
		DBG("pixel_format: %#x", set->crtc->primary->fb->pixel_format);

	if (set->mode)
		drm_mode_debug_printmodeline(set->mode);

	return drm_crtc_helper_set_config(set);
}

static int rockchip_crtc_page_flip(struct drm_crtc *crtc,
	struct drm_framebuffer *fb, struct drm_pending_vblank_event *event,
	uint32_t page_flip_flags)
{
	/* TODO: page flip */

	struct rockchip_crtc *rcrtc = to_rockchip_crtc(crtc);
	struct drm_device *drm = crtc->dev;
//	unsigned long flags;

	DBG("%#x", page_flip_flags);

//	spin_lock_irqsave(&drm->event_lock, flags);
	if (rcrtc->event) {
//		dev_err(drm->dev, "already pending page flip!\n");
//		spin_unlock_irqrestore(&drm->event_lock, flags);
		return -EBUSY;
	}
//	spin_unlock_irqrestore(&drm->event_lock, flags);

	crtc->primary->fb = fb;
	update_scanout(crtc);

	if (event) {
		int index = drm_crtc_index(crtc);
		event->pipe = index;
		drm_vblank_get(drm, index);
//		spin_lock_irqsave(&drm->event_lock, flags);
		rcrtc->event = event;
//		spin_unlock_irqrestore(&drm->event_lock, flags);
	}

	return 0;
}

static void rockchip_crtc_destroy(struct drm_crtc *crtc)
{
	struct rockchip_crtc *rcrtc = to_rockchip_crtc(crtc);

	DBG();

	rockchip_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);

	drm_crtc_cleanup(crtc);

	kfree(rcrtc);
}

static const struct drm_crtc_funcs rockchip_crtc_funcs = {
	.cursor_set	= rockchip_crtc_cursor_set,
	.cursor_move	= rockchip_crtc_cursor_move,
	.destroy	= rockchip_crtc_destroy,
	.set_config	= rockchip_crtc_set_config,
	.page_flip	= rockchip_crtc_page_flip,
};

static const uint32_t formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_RGB565,
};

/*
static const uint32_t cursor_formats[] = {
};
*/

struct drm_crtc *rockchip_crtc_create(struct drm_device *dev)
{
	struct rockchip_crtc *rcrtc;
	struct drm_crtc *crtc;
	struct drm_plane *primary, *cursor = NULL;
	int ret;

	rcrtc = kzalloc(sizeof(*rcrtc), GFP_KERNEL);
	if (!rcrtc) {
		dev_err(dev->dev, "allocation failed\n");
		return NULL;
	}

	crtc = &rcrtc->base;

	rcrtc->dpms = DRM_MODE_DPMS_OFF;

	primary = drm_primary_helper_create_plane(dev, formats,
							ARRAY_SIZE(formats));
	if (!primary)
		goto fail_primary;

	/* TODO: cursor plane */
/*
	cursor = kzalloc(sizeof(*cursor), GFP_KERNEL);
	if (!cursor)
		goto fail_cursor;

	drm_universal_plane_init(dev, cursor, 0, DRM_PLANE_TYPE_CURSOR);
*/
	ret = drm_crtc_init_with_planes(dev, crtc, primary, cursor,
					&rockchip_crtc_funcs);
	if (ret < 0)
		goto fail;

	drm_crtc_helper_add(crtc, &rockchip_crtc_helper_funcs);

	return crtc;

fail:
/*
	kfree(cursor);

fail_cursor:
*/
	drm_primary_helper_destroy(primary);

fail_primary:
	rockchip_crtc_destroy(crtc);
	return NULL;
}


