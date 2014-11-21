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

/* Rockchip LCDC DRM driver, based on tilcdc and imx-drm */

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>

#include "rockchip_lcdc_drv.h"
#include "rockchip_lcdc_regs.h"

static struct of_device_id rockchip_lcdc_of_match[];

static struct drm_framebuffer *rockchip_lcdc_fb_create(struct drm_device *dev,
		struct drm_file *file_priv, struct drm_mode_fb_cmd2 *mode_cmd)
{
	DBG();

	return drm_fb_cma_create(dev, file_priv, mode_cmd);
}

static void rockchip_lcdc_fb_output_poll_changed(struct drm_device *dev)
{
	struct rockchip_drm_priv *priv = dev->dev_private;

	DBG();

	drm_fbdev_cma_hotplug_event(priv->fbdev);
}

static const struct drm_mode_config_funcs mode_config_funcs = {
	.fb_create = rockchip_lcdc_fb_create,
	.output_poll_changed = rockchip_lcdc_fb_output_poll_changed,
};

static int modeset_init(struct drm_device *dev)
{
	struct rockchip_drm_priv *priv = dev->dev_private;

	DBG();

	drm_mode_config_init(dev);

	priv->crtc = rockchip_crtc_create(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = ROCKCHIP_LCDC_MAX_WIDTH;
	dev->mode_config.max_height = ROCKCHIP_LCDC_MAX_HEIGHT;
	dev->mode_config.funcs = &mode_config_funcs;

	return 0;
}

/*
 * DRM device
 */

static int rockchip_lcdc_unload(struct drm_device *dev)
{
	struct rockchip_drm_priv *priv = dev->dev_private;

	DBG();

	if (priv->cursor)
		drm_gem_cma_free_object(&priv->cursor->base);

	drm_kms_helper_poll_fini(dev);
	drm_fbdev_cma_fini(priv->fbdev);

	component_unbind_all(dev->dev, dev);

	pm_runtime_get_sync(dev->dev);
	drm_irq_uninstall(dev);
	pm_runtime_put_sync(dev->dev);

	drm_vblank_cleanup(dev);

	drm_mode_config_cleanup(dev);

	pm_runtime_disable(dev->dev);

	clk_put(priv->disp_clk);
	clk_put(priv->clk);

	if (priv->mmio)
		iounmap(priv->mmio);

	dev->dev_private = NULL;

	kfree(priv);

	return 0;
}

static int rockchip_lcdc_load(struct drm_device *drm, unsigned long flags)
{
	struct platform_device *pdev = drm->platformdev;
	struct device_node *node = pdev->dev.of_node;
	const struct of_device_id *match;
	struct rockchip_drm_priv *priv;
	struct resource *res;
	struct device *dev = drm->dev;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(dev, "failed to allocate private data\n");
		return -ENOMEM;
	}

	drm->dev_private = priv;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to get memory resource\n");
		ret = -EINVAL;
		goto fail_free_priv;
	}

	priv->mmio = ioremap_nocache(res->start, resource_size(res));
	if (!priv->mmio) {
		dev_err(dev, "failed to ioremap\n");
		ret = -ENOMEM;
		goto fail_free_priv;
	}

	priv->clk = clk_get(dev, "lcdc_hclk");
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get functional clock\n");
		ret = -ENODEV;
		goto fail_iounmap;
	}

	priv->disp_clk = clk_get(dev, "lcdc_dclk");
	if (IS_ERR(priv->disp_clk)) {
		dev_err(dev, "failed to get display clock\n");
		ret = -ENODEV;
		goto fail_put_clk;
	}

	pm_runtime_enable(dev);

	/* Get LCDC version */
	match = of_match_node(rockchip_lcdc_of_match, node);
	priv->rev = (int) match->data;

	ret = modeset_init(drm);
	if (ret < 0) {
		dev_err(dev, "failed to initialize mode setting\n");
		goto fail_put_disp_clk;
	}

	ret = drm_vblank_init(drm, 1);
	if (ret < 0) {
		dev_err(dev, "failed to initialize vblank\n");
		goto fail_mode_config_cleanup;
	}

	pm_runtime_get_sync(dev);
	ret = drm_irq_install(drm, platform_get_irq(pdev, 0));
	pm_runtime_put_sync(dev);
	if (ret < 0) {
		dev_err(dev, "failed to install IRQ handler\n");
		goto fail_vblank_cleanup;
	}

	platform_set_drvdata(pdev, drm);

	/* Now try and bind all our sub-components */
	ret = component_bind_all(dev, drm);
	if (ret)
		goto fail_irq_uninstall;

	DBG("num_crtc: %d", drm->mode_config.num_crtc);
	DBG("num_connector: %d", drm->mode_config.num_connector);

	priv->fbdev = drm_fbdev_cma_init(drm, 32, drm->mode_config.num_crtc,
					 drm->mode_config.num_connector);
	if (IS_ERR(priv->fbdev)) {
		ret = PTR_ERR(priv->fbdev);
		goto fail_unbind;
	}

	drm_kms_helper_poll_init(drm);

	return 0;

fail_unbind:
	component_unbind_all(dev, drm);

fail_irq_uninstall:
	pm_runtime_get_sync(dev);
	drm_irq_uninstall(drm);
	pm_runtime_put_sync(dev);

fail_vblank_cleanup:
	drm_vblank_cleanup(drm);

fail_mode_config_cleanup:
	drm_mode_config_cleanup(drm);

fail_put_disp_clk:
	pm_runtime_disable(dev);
	clk_put(priv->disp_clk);

fail_put_clk:
	clk_put(priv->clk);

fail_iounmap:
	iounmap(priv->mmio);

fail_free_priv:
	drm->dev_private = NULL;
	kfree(priv);
	return ret;
}

static void rockchip_lcdc_preclose(struct drm_device *dev, struct drm_file *file)
{
	DBG();
}

static void rockchip_lcdc_lastclose(struct drm_device *dev)
{
	DBG();
}

static irqreturn_t rockchip_lcdc_irq(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct rockchip_drm_priv *priv = dev->dev_private;

	return rockchip_crtc_irq(priv->crtc);
}

static void rockchip_lcdc_irq_preinstall(struct drm_device *dev)
{
	DBG();

	/* disable and clear all interrupts */
	rockchip_lcdc_write(dev, LCDC_INT_STATUS, 0xf00);
}

static int rockchip_lcdc_irq_postinstall(struct drm_device *dev)
{
	DBG();

	return 0;
}

static void rockchip_lcdc_irq_uninstall(struct drm_device *dev)
{
	DBG();
}

static int rockchip_lcdc_enable_vblank(struct drm_device *dev, int crtc)
{
//	unsigned long flags;

	DBG();

//	spin_lock_irqsave(&dev->event_lock, flags);
	rockchip_lcdc_enable_irqstatus(dev, FS_INT);
//	spin_unlock_irqrestore(&dev->event_lock, flags); /* FIXME */

	return 0;
}

static void rockchip_lcdc_disable_vblank(struct drm_device *dev, int crtc)
{
//	unsigned long flags;

	DBG();

//	spin_lock_irqsave(&dev->event_lock, flags);
	rockchip_lcdc_disable_irqstatus(dev, FS_INT);
//	spin_unlock_irqrestore(&dev->event_lock, flags);
}

#if defined(CONFIG_DEBUG_FS) || defined(CONFIG_PM_SLEEP)
static const struct {
	const char *name;
	uint8_t  rev;
	uint32_t reg;
} registers[] =	{
#define REG(rev, reg) { #reg, rev, reg }
	/* revision 1: RK3066 */

	/* TODO: RK3066 registers */

	/* revision 2: RK3188 */
	REG(RK3188_LCDC, LCDC_SYS_CTRL),
	REG(RK3188_LCDC, LCDC_DSP_CTRL0),
	REG(RK3188_LCDC, LCDC_DSP_CTRL1),
	REG(RK3188_LCDC, LCDC_MCU_CTRL),
	REG(RK3188_LCDC, LCDC_INT_STATUS),
	REG(RK3188_LCDC, LCDC_ALPHA_CTRL),
	REG(RK3188_LCDC, LCDC_WIN0_COLOR_KEY),
	REG(RK3188_LCDC, LCDC_WIN1_COLOR_KEY),
	REG(RK3188_LCDC, LCDC_WIN0_COLOR_KEY),
	REG(RK3188_LCDC, LCDC_WIN1_COLOR_KEY),
	REG(RK3188_LCDC, LCDC_WIN0_YRGB_MST0),
	REG(RK3188_LCDC, LCDC_WIN0_CBR_MST0),
	REG(RK3188_LCDC, LCDC_WIN0_YRGB_MST1),
	REG(RK3188_LCDC, LCDC_WIN0_CBR_MST1),
	REG(RK3188_LCDC, LCDC_WIN_VIR),
	REG(RK3188_LCDC, LCDC_WIN0_ACT_INFO),
	REG(RK3188_LCDC, LCDC_WIN0_DSP_INFO),
	REG(RK3188_LCDC, LCDC_WIN0_DSP_ST),
	REG(RK3188_LCDC, LCDC_WIN0_SCL_FACTOR_YRGB),
	REG(RK3188_LCDC, LCDC_WIN0_SCL_FACTOR_CBR),
	REG(RK3188_LCDC, LCDC_WIN0_SCL_OFFSET),
	REG(RK3188_LCDC, LCDC_WIN1_MST),
	REG(RK3188_LCDC, LCDC_WIN1_DSP_INFO),
	REG(RK3188_LCDC, LCDC_WIN1_DSP_ST),
	REG(RK3188_LCDC, LCDC_HWC_MST),
	REG(RK3188_LCDC, LCDC_HWC_DSP_ST),
	REG(RK3188_LCDC, LCDC_HWC_COLOR_LUT0),
	REG(RK3188_LCDC, LCDC_HWC_COLOR_LUT1),
	REG(RK3188_LCDC, LCDC_HWC_COLOR_LUT2),
	REG(RK3188_LCDC, LCDC_DSP_HTOTAL_HS_END),
	REG(RK3188_LCDC, LCDC_DSP_HACT_ST_END),
	REG(RK3188_LCDC, LCDC_DSP_VTOTAL_VS_END),
	REG(RK3188_LCDC, LCDC_DSP_VACT_ST_END),
	REG(RK3188_LCDC, LCDC_DSP_VS_ST_END_F1),
	REG(RK3188_LCDC, LCDC_DSP_VACT_ST_END_F1),
#undef REG
};
#endif

#ifdef CONFIG_DEBUG_FS
static int rockchip_lcdc_regs_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct rockchip_drm_priv *priv = dev->dev_private;
	unsigned i;

	pm_runtime_get_sync(dev->dev);

	seq_printf(m, "revision: %d\n", priv->rev);

	for (i = 0; i < ARRAY_SIZE(registers); i++)
		if (priv->rev == registers[i].rev)
			seq_printf(m, "%s:\t %#x\n", registers[i].name,
				rockchip_lcdc_read(dev, registers[i].reg));

	pm_runtime_put_sync(dev->dev);

	return 0;
}

static int rockchip_lcdc_mm_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	return drm_mm_dump_table(m, &dev->vma_offset_manager->vm_addr_space_mm);
}

static struct drm_info_list rockchip_lcdc_debugfs_list[] = {
	{ "regs", rockchip_lcdc_regs_show, 0 },
	{ "mm",   rockchip_lcdc_mm_show,   0 },
	{ "fb",   drm_fb_cma_debugfs_show, 0 },
};

static int rockchip_lcdc_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	int ret;

	ret = drm_debugfs_create_files(rockchip_lcdc_debugfs_list,
			ARRAY_SIZE(rockchip_lcdc_debugfs_list),
			minor->debugfs_root, minor);

	if (ret) {
		dev_err(dev->dev, "could not install rockchip_lcdc_debugfs_list\n");
	}

	return ret;
}

static void rockchip_lcdc_debugfs_cleanup(struct drm_minor *minor)
{
	drm_debugfs_remove_files(rockchip_lcdc_debugfs_list,
			ARRAY_SIZE(rockchip_lcdc_debugfs_list), minor);
}
#endif

static const struct file_operations fops = {
	.owner              = THIS_MODULE,
	.open               = drm_open,
	.release            = drm_release,
	.unlocked_ioctl     = drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl       = drm_compat_ioctl,
#endif
	.poll               = drm_poll,
	.read               = drm_read,
	.llseek             = no_llseek,
	.mmap               = drm_gem_cma_mmap,
};

static struct drm_driver rockchip_lcdc_driver = {
	.load               = rockchip_lcdc_load,
	.unload             = rockchip_lcdc_unload,
	.preclose           = rockchip_lcdc_preclose,
	.lastclose          = rockchip_lcdc_lastclose,
	.set_busid          = drm_platform_set_busid,
	.irq_handler        = rockchip_lcdc_irq,
	.irq_preinstall     = rockchip_lcdc_irq_preinstall,
	.irq_postinstall    = rockchip_lcdc_irq_postinstall,
	.irq_uninstall      = rockchip_lcdc_irq_uninstall,
	.get_vblank_counter = drm_vblank_count,
	.enable_vblank      = rockchip_lcdc_enable_vblank,
	.disable_vblank     = rockchip_lcdc_disable_vblank,
	.gem_free_object    = drm_gem_cma_free_object,
	.gem_vm_ops         = &drm_gem_cma_vm_ops,
	.dumb_create        = drm_gem_cma_dumb_create,
	.dumb_map_offset    = drm_gem_cma_dumb_map_offset,
	.dumb_destroy       = drm_gem_dumb_destroy,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init       = rockchip_lcdc_debugfs_init,
	.debugfs_cleanup    = rockchip_lcdc_debugfs_cleanup,
#endif
	.fops               = &fops,
	.name               = "rockchip-lcdc",
	.desc               = "Rockchip LCD Controller DRM",
	.date               = "20141118",
	.major              = 1,
	.minor              = 0,
	.driver_features    = DRIVER_HAVE_IRQ | DRIVER_GEM | DRIVER_MODESET,
};

static int compare_of(struct device *dev, void *data)
{
	struct device_node *np = data;

	return dev->of_node == np;
}

static int rockchip_lcdc_bind(struct device *dev)
{
	return drm_platform_init(&rockchip_lcdc_driver, to_platform_device(dev));
}

static void rockchip_lcdc_unbind(struct device *dev)
{
	drm_put_dev(dev_get_drvdata(dev));
}

static const struct component_master_ops rockchip_lcdc_ops = {
	.bind = rockchip_lcdc_bind,
	.unbind = rockchip_lcdc_unbind,
};

static int rockchip_lcdc_platform_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *prev = NULL;
	struct component_match *match = NULL;

	DBG();

	/* Iterate over the endpoints and create one encoder for each */
	while (1) {
		struct device_node *ep_node;
		struct device_node *ep_node_parent;
		struct of_endpoint ep;
		int ret;

		ep_node = of_graph_get_next_endpoint(np, prev);
		of_node_put(prev);
		prev = ep_node;

		if (ep_node == NULL)
			break;

		ret = of_graph_parse_endpoint(ep_node, &ep);
		if (ret < 0) {
			of_node_put(ep_node);
			continue;
		}

		ep_node_parent = of_graph_get_remote_port_parent(ep_node);
		of_node_put(ep_node_parent);

		DBG("%s <-> %s", of_node_full_name(ep_node),
			of_node_full_name(ep_node_parent));

		component_match_add(&pdev->dev, &match, compare_of, ep_node_parent);
	}

	if (match == NULL)
		return -ENODEV;

	return component_master_add_with_match(&pdev->dev, &rockchip_lcdc_ops, match);
}

static int rockchip_lcdc_platform_remove(struct platform_device *pdev)
{
	DBG();

	component_master_del(&pdev->dev, &rockchip_lcdc_ops);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rockchip_lcdc_pm_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	/* The drm_dev is NULL before .load hook is called */
	if (drm == NULL)
		return 0;

	drm_kms_helper_poll_disable(drm);

	return 0;
}

static int rockchip_lcdc_pm_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	if (drm == NULL)
		return 0;

	drm_helper_resume_force_mode(drm);
	drm_kms_helper_poll_enable(drm);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(rockchip_lcdc_pm_ops, rockchip_lcdc_pm_suspend,
	rockchip_lcdc_pm_resume);

static struct of_device_id rockchip_lcdc_of_match[] = {
	{ .compatible = "rockchip,rk3066-lcdc", .data = (void *)RK3066_LCDC},
	{ .compatible = "rockchip,rk3188-lcdc", .data = (void *)RK3188_LCDC},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rockchip_lcdc_of_match);

static struct platform_driver rockchip_lcdc_platform_driver = {
	.probe      = rockchip_lcdc_platform_probe,
	.remove     = rockchip_lcdc_platform_remove,
	.driver     = {
		.name   = "rockchip-lcdc",
		.pm     = &rockchip_lcdc_pm_ops,
		.of_match_table = rockchip_lcdc_of_match,
	},
};
module_platform_driver(rockchip_lcdc_platform_driver);

MODULE_AUTHOR("Julien Chauveau <julien.chauveau@neo-technologies.fr>");
MODULE_DESCRIPTION("Rockchip LCD Controller DRM Driver");
MODULE_LICENSE("GPL");
