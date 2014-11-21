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
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/videodev2.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include "rockchip_lcdc_drv.h"

struct rockchip_panel {
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct device *dev;
	void *edid;
	int edid_len;
	u32 interface_pix_fmt;
	int mode_valid;
	struct drm_display_mode mode;
	struct drm_panel *panel;
};

#define con_to_rkp(x) container_of(x, struct rockchip_panel, connector)
#define enc_to_rkp(x) container_of(x, struct rockchip_panel, encoder)


/*
 * DRM connector helper functions
 */

static enum drm_connector_status panel_connector_detect(
		struct drm_connector *connector, bool force)
{
	DBG();

	return connector_status_connected;
}

static void panel_connector_destroy(struct drm_connector *connector)
{
	DBG();

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static struct drm_connector_funcs panel_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = panel_connector_detect,
	.destroy = panel_connector_destroy,
};

/*
 * DRM connector functions
 */

static int panel_connector_get_modes(struct drm_connector *connector)
{
	struct rockchip_panel *rkp = con_to_rkp(connector);
	struct device_node *np = rkp->dev->of_node;
	int modes, num_modes = 0;

	DBG();

	modes = drm_panel_get_modes(rkp->panel);
	if (modes > 0)
		return modes;

	if (rkp->edid) {
		drm_mode_connector_update_edid_property(connector, rkp->edid);
		num_modes = drm_add_edid_modes(connector, rkp->edid);
	}

	if (rkp->mode_valid) {
		struct drm_display_mode *mode = drm_mode_create(connector->dev);

		if (!mode)
			return -EINVAL;
		drm_mode_copy(mode, &rkp->mode);
		mode->type |= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
		drm_mode_probed_add(connector, mode);
		num_modes++;
	}

	if (np) {
		struct drm_display_mode *mode = drm_mode_create(connector->dev);

		if (!mode)
			return -EINVAL;
		of_get_drm_display_mode(np, &rkp->mode, OF_USE_NATIVE_MODE);
		drm_mode_copy(mode, &rkp->mode);
		mode->type |= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
		drm_mode_probed_add(connector, mode);
		num_modes++;
	}

	DBG("num_modes: %d", num_modes);

	return num_modes;
}

static struct drm_encoder *panel_connector_best_encoder(
		struct drm_connector *connector)
{
	struct rockchip_panel *rkp = con_to_rkp(connector);

	DBG();

	return &rkp->encoder;
}

static struct drm_connector_helper_funcs panel_connector_helper_funcs = {
	.get_modes = panel_connector_get_modes,
	.best_encoder = panel_connector_best_encoder,
};


/*
 * DRM encoder helper functions
 */

static void panel_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct rockchip_panel *rkp = enc_to_rkp(encoder);

	DBG();

	if (mode != DRM_MODE_DPMS_ON)
		drm_panel_disable(rkp->panel);
	else
		drm_panel_enable(rkp->panel);
}

static bool panel_encoder_mode_fixup(struct drm_encoder *encoder,
			   const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode)
{
	DBG();

	return true;
}

static void panel_encoder_prepare(struct drm_encoder *encoder)
{
	struct rockchip_panel *rkp = enc_to_rkp(encoder);

	DBG();

	drm_panel_prepare(rkp->panel);
}

static void panel_encoder_commit(struct drm_encoder *encoder)
{
	struct rockchip_panel *rkp = enc_to_rkp(encoder);

	DBG();

	drm_panel_enable(rkp->panel);
}

static void panel_encoder_mode_set(struct drm_encoder *encoder,
			 struct drm_display_mode *mode,
			 struct drm_display_mode *adjusted_mode)
{
	DBG();
}

static void panel_encoder_disable(struct drm_encoder *encoder)
{
	struct rockchip_panel *rkp = enc_to_rkp(encoder);

	DBG();

	drm_panel_disable(rkp->panel);
	drm_panel_unprepare(rkp->panel);
}

static struct drm_encoder_helper_funcs panel_encoder_helper_funcs = {
	.dpms = panel_encoder_dpms,
	.mode_fixup = panel_encoder_mode_fixup,
	.prepare = panel_encoder_prepare,
	.commit = panel_encoder_commit,
	.mode_set = panel_encoder_mode_set,
	.disable = panel_encoder_disable,
};


/*
 * DRM encoder functions
 */

static void panel_encoder_destroy(struct drm_encoder *encoder)
{
	DBG();

	drm_encoder_cleanup(encoder);
}

static struct drm_encoder_funcs panel_encoder_funcs = {
	.destroy = panel_encoder_destroy,
};

static int panel_register(struct drm_device *drm,
	struct rockchip_panel *rkp)
{
	int ret;

	DBG();

	rkp->encoder.possible_crtcs = drm_of_find_possible_crtcs(drm,
		rkp->dev->of_node);

	DBG("possible_crtcs: %d", rkp->encoder.possible_crtcs);

	/* FIXME: */
	rkp->encoder.possible_crtcs = 1;

	/* FIXME: this is the mask of outputs which can clone this output. */
	rkp->encoder.possible_clones = ~0;

	/* set the connector's dpms to OFF so that
	 * drm_helper_connector_dpms() won't return
	 * immediately since the current state is ON
	 * at this point.
	 */
	rkp->connector.dpms = DRM_MODE_DPMS_OFF;

	drm_encoder_helper_add(&rkp->encoder, &panel_encoder_helper_funcs);
	ret = drm_encoder_init(drm, &rkp->encoder, &panel_encoder_funcs,
			 DRM_MODE_ENCODER_LVDS);
	if (ret)
		goto err_encoder;


	drm_connector_helper_add(&rkp->connector,
			&panel_connector_helper_funcs);
	ret = drm_connector_init(drm, &rkp->connector, &panel_connector_funcs,
			   DRM_MODE_CONNECTOR_LVDS);
	if (ret)
		goto err_connector;

	ret = drm_connector_register(&rkp->connector);
	if (ret)
		goto err_sysfs;

	if (rkp->panel)
		drm_panel_attach(rkp->panel, &rkp->connector);

	drm_mode_connector_attach_encoder(&rkp->connector, &rkp->encoder);

	rkp->connector.encoder = &rkp->encoder;

	return 0;

err_sysfs:
	drm_connector_cleanup(&rkp->connector);
err_connector:
	drm_encoder_cleanup(&rkp->encoder);
err_encoder:
	panel_encoder_destroy(&rkp->encoder);
	return ret;
}

static int rockchip_panel_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = data;
	struct device_node *np = dev->of_node;
	struct device_node *panel_node;
	const u8 *edidp;
	struct rockchip_panel *rkp;
	int ret;
	const char *fmt;

	DBG();

	rkp = devm_kzalloc(dev, sizeof(*rkp), GFP_KERNEL);
	if (!rkp)
		return -ENOMEM;

	edidp = of_get_property(np, "edid", &rkp->edid_len);
	if (edidp)
		rkp->edid = kmemdup(edidp, rkp->edid_len, GFP_KERNEL);

	ret = of_property_read_string(np, "interface-pix-fmt", &fmt);
	if (!ret) {
		if (!strcmp(fmt, "rgb24"))
			rkp->interface_pix_fmt = V4L2_PIX_FMT_RGB24;
		else if (!strcmp(fmt, "rgb565"))
			rkp->interface_pix_fmt = V4L2_PIX_FMT_RGB565;
		else if (!strcmp(fmt, "bgr666"))
			rkp->interface_pix_fmt = V4L2_PIX_FMT_BGR666;
		else if (!strcmp(fmt, "lvds666"))
			rkp->interface_pix_fmt =
					v4l2_fourcc('L', 'V', 'D', '6');
	}

	rkp->panel = of_drm_find_panel(panel_node);

	rkp->dev = dev;

	ret = panel_register(drm, rkp);
	if (ret)
		return ret;

	dev_set_drvdata(dev, rkp);

	return 0;
}

static void rockchip_panel_unbind(struct device *dev, struct device *master,
	void *data)
{
	struct rockchip_panel *rkp = dev_get_drvdata(dev);

	DBG();

	rkp->encoder.funcs->destroy(&rkp->encoder);
	rkp->connector.funcs->destroy(&rkp->connector);
}

static const struct component_ops rockchip_panel_ops = {
	.bind	= rockchip_panel_bind,
	.unbind	= rockchip_panel_unbind,
};

static int rockchip_panel_probe(struct platform_device *pdev)
{
	DBG();

	return component_add(&pdev->dev, &rockchip_panel_ops);
}

static int rockchip_panel_remove(struct platform_device *pdev)
{
	DBG();

	component_del(&pdev->dev, &rockchip_panel_ops);
	return 0;
}

static const struct of_device_id rockchip_panel_dt_ids[] = {
	{ .compatible = "rockchip,lcdc-panel", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rockchip_panel_dt_ids);

static struct platform_driver rockchip_panel_driver = {
	.probe		= rockchip_panel_probe,
	.remove		= rockchip_panel_remove,
	.driver		= {
		.of_match_table = rockchip_panel_dt_ids,
		.name	= "rockchip-lcdc-panel",
	},
};

module_platform_driver(rockchip_panel_driver);

MODULE_AUTHOR("Julien Chauveau <julien.chauveau@neo-technologies.fr>");
MODULE_DESCRIPTION("Rockchip LCDC panel driver");
MODULE_LICENSE("GPL");

