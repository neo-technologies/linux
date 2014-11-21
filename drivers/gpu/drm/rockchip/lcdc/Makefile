# FIXME
#ifeq (, $(findstring -W,$(EXTRA_CFLAGS)))
#	ccflags-y += -Werror
#endif

rockchip_lcdc-y := \
	rockchip_lcdc_crtc.o \
	rockchip_lcdc_drv.o

obj-$(CONFIG_DRM_ROCKCHIP_LCDC)		+= rockchip_lcdc.o
obj-$(CONFIG_DRM_ROCKCHIP_LCDC_PANEL)	+= rockchip_lcdc_panel.o
