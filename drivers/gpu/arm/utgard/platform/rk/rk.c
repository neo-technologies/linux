
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/mali/mali_utgard.h>

static const struct of_device_id mali_dt_ids[] = {
	{ .compatible = "arm,mali400mp" },
	{ .compatible = "arm,mali-utgard" },
	{ /* sentinel */ }
};

/* for mmu and os memory */
#define MEM_BASE_ADDR    0x60000000
#define MEM_TOTAL_SIZE   0x00800000

/* for shared memory */
#define CONFIG_MALI_MEM_SIZE 256
#define MEM_MALI_SIZE 	 CONFIG_MALI_MEM_SIZE*1024*1024
//#define MEM_MALI_BASE    0x80000000 - MEM_MALI_SIZE

/* FIXME: get this from devicetree */
static struct mali_gpu_device_data mali_gpu_data = {
		.shared_mem_size = MEM_MALI_SIZE,
/* FIXME: why does utgard need a specific framebuffer address,
 * while midgard can solve this on its own.
 * Using these values like this is most likely completely wrong.
 */
		.fb_start = MEM_BASE_ADDR,
		.fb_size  = MEM_TOTAL_SIZE,
//		.dedicated_mem_start = CONFIG_MALI_MEM_SIZE*1024*1024,
//		.dedicated_mem_size = MEM_MALI_BASE
};

struct mali_platform_rk {
	struct clk *clk;
	struct device_node *np;
	struct platform_device *pdev;
};

static struct mali_platform_rk mali_rk;

static void rk_mali_create_resources(struct resource *res, resource_size_t address, int *irqs)
{
	struct resource target[] = {
		MALI_GPU_RESOURCES_MALI400_MP4(
			address,
			irqs[0], irqs[1],
			irqs[2], irqs[1],
			irqs[2], irqs[1],
			irqs[2], irqs[1],
			irqs[2], irqs[1]
		)
	};

	memcpy(res, &target, sizeof(struct resource) * 21);
}

int mali_platform_device_register(void)
{
	struct device_node *np, *mem_node;
	struct platform_device *mali_device;
	struct resource res;
	struct resource target[21];
	const struct of_device_id *match;
	resource_size_t address;
	int irqs[3], ret;

	np = of_find_matching_node_and_match(NULL, mali_dt_ids, &match);
	if (!np) {
		pr_err("%s: could not find matching mali device node\n", __func__);
		return -ENODEV;
	}

	ret = of_address_to_resource(np, 0, &res);
	if (ret < 0)
		return ret;
	address = res.start;

	irqs[0] = of_irq_get_byname(np, "GP");
	if (irqs[0] < 0)
		return irqs[0];

	irqs[1] = of_irq_get_byname(np, "MMU");
	if (irqs[1] < 0)
		return irqs[1];

	irqs[2] = of_irq_get_byname(np, "PP");
	if (irqs[2] < 0)
		return irqs[2];

	rk_mali_create_resources(&target[0], address, irqs);

	mem_node = of_parse_phandle(np, "memory-region", 0);
	if (!mem_node) {
		pr_err("%s: could not find memory region\n", __func__);
		return -ENODEV;
	}

	of_node_put(np);

	mali_device = platform_device_alloc("mali-utgard", 0);
	if (mali_device == NULL)
		return -ENOMEM;

	mali_device->dev.dma_mask = &mali_device->dev.coherent_dma_mask,
	mali_device->dev.coherent_dma_mask = DMA_BIT_MASK(32),

	ret = platform_device_add_resources(mali_device, target, ARRAY_SIZE(target));
	if (ret < 0) {
		pr_err("%s: could not add platform device resources, %d\n", __func__, ret);
		goto err_pdev_put;
	}

	ret = platform_device_add_data(mali_device, &mali_gpu_data,
			sizeof(mali_gpu_data));
	if (ret < 0) {
		pr_err("%s: could not add platform device data, %d\n", __func__, ret);
		goto err_pdev_put;
	}

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_set_autosuspend_delay(&mali_device->dev, 1000);
	pm_runtime_use_autosuspend(&mali_device->dev);
	pm_runtime_enable(&mali_device->dev);
#endif

	/* FIXME: Not sure if this is the right place. */
//	gpu_power_domain_control(1);
//	mali_platform_init();

	mali_rk.np = np;
	mali_rk.pdev = mali_device;

	mali_rk.clk = of_clk_get_by_name(np, "aclk_gpu");
	if (IS_ERR(mali_rk.clk)) {
		ret = PTR_ERR(mali_rk.clk);
		pr_err("%s: could not get aclk_cpu, %d\n", __func__, ret);
		goto err_pdev_put;
	}

	ret = clk_prepare_enable(mali_rk.clk);
	if (ret < 0) {
		pr_err("%s: could not get aclk_cpu, %d\n", __func__, ret);
		goto err_clk_put;

	}

	ret = platform_device_add(mali_device);
	if (ret < 0) {
		pr_err("%s: could not register platform device, %d\n", __func__, ret);
		goto err_clk_disable;
	}

	return 0;

err_clk_disable:
	clk_disable_unprepare(mali_rk.clk);
err_clk_put:
	clk_put(mali_rk.clk);
err_pdev_put:
	platform_device_put(mali_device);
	return ret;
}


void mali_platform_device_unregister(void)
{
	platform_device_unregister(mali_rk.pdev);

	clk_disable_unprepare(mali_rk.clk);
	clk_put(mali_rk.clk);
}



#if 0

///////////////////////////////



#ifdef MALI_CFG_REGULATOR

void mali_regulator_set_voltage(int min_uV, int max_uV)
{
	int voltage;
	min_uV = mali_gpu_vol;
	max_uV = mali_gpu_vol;
#if MALI_VOLTAGE_LOCK
	if (mali_vol_lock_flag == MALI_FALSE) {
		if (min_uV < MALI_BOTTOMLOCK_VOL || max_uV < MALI_BOTTOMLOCK_VOL) {
			min_uV = MALI_BOTTOMLOCK_VOL;
			max_uV = MALI_BOTTOMLOCK_VOL;
		}
	} else if (_mali_osk_atomic_read(&voltage_lock_status) > 0 ) {
		if (min_uV < mali_lock_vol || max_uV < mali_lock_vol) {
		
			min_uV = mali_lock_vol;
			max_uV = mali_lock_vol;
			
		}
	}
#endif

	_mali_osk_lock_wait(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);

	if( IS_ERR_OR_NULL(g3d_regulator) )
	{
		MALI_DEBUG_PRINT(1, ("error on mali_regulator_set_voltage : g3d_regulator is null\n"));
		return;
	}
	MALI_DEBUG_PRINT(2, ("= regulator_set_voltage: %d, %d \n",min_uV, max_uV));

	regulator_set_voltage(g3d_regulator,min_uV,max_uV);
	voltage = regulator_get_voltage(g3d_regulator);

	mali_gpu_vol = voltage;
	MALI_DEBUG_PRINT(1, ("= regulator_get_voltage: %d \n",mali_gpu_vol));

	_mali_osk_lock_signal(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
}
#endif


_mali_osk_errcode_t gpu_power_domain_control(int bpower_on) {
	if (bpower_on) {
		u32 timeout;
//		pmu_set_power_domain(PD_GPU,1);
		timeout = 10;
		while (!pmu_power_domain_is_on(PD_GPU)){
			if (timeout == 0) {
				MALI_PRINTF(("Power domain  enable failed.\n"));
				return -ETIMEDOUT;
			}
			timeout--;
			_mali_osk_time_ubusydelay(100);
		}
		
		
	} else {
		u32 timeout;
//		pmu_set_power_domain(PD_GPU,0);
		/* Wait max 1ms */
		timeout = 10;
		while (pmu_power_domain_is_on(PD_GPU)) {
			if (timeout == 0) {
				MALI_PRINTF(("Power domain  disable failed.\n" ));
				return -ETIMEDOUT;
			}
			timeout--;
			_mali_osk_time_ubusydelay(100);
		}

	}

	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_init(void) {
	MALI_DEBUG_PRINT(4, ("mali_platform_init() called\n"));
	MALI_CHECK(init_mali_clock(), _MALI_OSK_ERR_FAULT);
#if MALI_VOLTAGE_LOCK
	_mali_osk_atomic_init(&voltage_lock_status, 0);
#endif

	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_powerdown(u32 cores) {
	MALI_DEBUG_PRINT(3,
			("power down is called in mali_platform_powerdown state %x core %x \n", gpu_power_state, cores));

	if (gpu_power_state != 0) // power down after state is 0
			{
		gpu_power_state = gpu_power_state & (~cores);
		if (gpu_power_state == 0) {
			MALI_DEBUG_PRINT( 3, ("disable clock\n"));
			clk_disable(mali_rk.clk);
		}
	} else {
		MALI_PRINT(
				("mali_platform_powerdown gpu_power_state == 0 and cores %x \n", cores));
	}

	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_powerup(u32 cores) {
	MALI_DEBUG_PRINT(3,
			("power up is called in mali_platform_powerup state %x core %x \n", gpu_power_state, cores));

	if (gpu_power_state == 0) // power up only before state is 0
			{
		gpu_power_state = gpu_power_state | cores;

		if (gpu_power_state != 0) {
			MALI_DEBUG_PRINT(4, ("enable clock \n"));
		err = clk_enable(mali_rk.clk);
		}
	} else {
		gpu_power_state = gpu_power_state | cores;
	}

	MALI_SUCCESS;
}
void mali_gpu_utilization_handler(u32 utilization) {
	if (bPoweroff == 0) {
	}
}

#if MALI_POWER_MGMT_TEST_SUITE
u32 pmu_get_power_up_down_info(void)
{
	return 4095;
}

#endif


#if MALI_VOLTAGE_LOCK
int mali_voltage_lock_push(int lock_vol)
{
	int prev_status = _mali_osk_atomic_read(&voltage_lock_status);

	if (prev_status < 0) {
		MALI_PRINT(("gpu voltage lock status is not valid for push\n"));
		return -1;
	}
	if (prev_status == 0) {
		mali_lock_vol = lock_vol;
		if (mali_gpu_vol < mali_lock_vol)
		mali_regulator_set_voltage(mali_lock_vol, mali_lock_vol);
	} else {
		MALI_PRINT(("gpu voltage lock status is already pushed, current lock voltage : %d\n", mali_lock_vol));
		return -1;
	}

	return _mali_osk_atomic_inc_return(&voltage_lock_status);
}

int mali_voltage_lock_pop(void)
{
	if (_mali_osk_atomic_read(&voltage_lock_status) <= 0) {
		MALI_PRINT(("gpu voltage lock status is not valid for pop\n"));
		return -1;
	}
	return _mali_osk_atomic_dec_return(&voltage_lock_status);
}

int mali_voltage_lock_init(void)
{
	mali_vol_lock_flag = MALI_TRUE;

	MALI_SUCCESS;
}
#endif
/* platform functions end */


void mali_platform_device_unregister(void) {
	MALI_DEBUG_PRINT(4, ("mali_platform_device_unregister() called\n"));
	deinit_mali_clock();
	gpu_power_domain_control(0);
	platform_device_unregister(&mali_gpu_device);
}


mali_bool mali_clk_get(void) {
	MALI_DEBUG_PRINT(4, ("mali_clk_get() called\n"));
	// mali clock get always.
	if (mali_clock == NULL) {
		mali_clock = clk_get(NULL, GPUCLK_NAME);

		if (IS_ERR(mali_clock)) {
			MALI_PRINT( ("MALI Error : failed to get source mali clock\n"));
			return MALI_FALSE;
		}
	}

	return MALI_TRUE;
}

mali_bool mali_clk_set_rate(unsigned int clk, unsigned int mhz) {
	unsigned long rate = 0;

	clk = mali_gpu_clk;

	MALI_DEBUG_PRINT(4, ("mali_clk_set_rate() called\n"));
	//_mali_osk_lock_wait(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);

	if (mali_clk_get() == MALI_FALSE)
		return MALI_FALSE;

	rate = (unsigned long) clk * (unsigned long) mhz;
	MALI_DEBUG_PRINT(3, ("= clk_set_rate : %d , %d \n",clk, mhz ));

	if (clk_enable(mali_clock) < 0)
		return MALI_FALSE;

	clk_set_rate(mali_clock, rate);
	rate = clk_get_rate(mali_clock);

		mali_gpu_clk = (int) (rate / mhz);

	GPU_MHZ = mhz;
	MALI_DEBUG_PRINT(3, ("= clk_get_rate: %d \n",mali_gpu_clk));

/*	if (mali_clock != NULL) {
		clk_put(mali_clock);
	} */

	//_mali_osk_lock_signal(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);

	return MALI_TRUE;
}

mali_bool init_mali_clock(void) {
	mali_bool ret = MALI_TRUE;

	gpu_power_state = 0;
	MALI_DEBUG_PRINT(4, ("init_mali_clock() called\n"));
	if (mali_clock != 0)
		return ret; // already initialized

	mali_dvfs_lock = _mali_osk_lock_init(
			_MALI_OSK_LOCKFLAG_NONINTERRUPTABLE | _MALI_OSK_LOCKFLAG_ONELOCK, 0,
			0);
	if (mali_dvfs_lock == NULL)
		return _MALI_OSK_ERR_FAULT;

	if (mali_clk_get() == MALI_FALSE)
		return MALI_FALSE;

	clk_set_rate(mali_clock, GPUMINCLK);

	if (mali_clk_set_rate(mali_gpu_clk, GPU_MHZ) == MALI_FALSE) {
		ret = MALI_FALSE;
		goto err_clock_get;
	}

	MALI_PRINT(("init_mali_clock mali_clock %p \n", mali_clock));

#ifdef MALI_CFG_REGULATOR
#if USING_MALI_PMM
	g3d_regulator = regulator_get(&mali_gpu_device.dev, "vdd_core");
#else
	g3d_regulator = regulator_get(NULL, "vdd_core");
#endif

	if (IS_ERR(g3d_regulator))
	{
		MALI_PRINT( ("MALI Error : failed to get vdd_g3d\n"));
		ret = MALI_FALSE;
		goto err_regulator;
	}

//	regulator_enable(g3d_regulator);

	MALI_DEBUG_PRINT(1, ("= regulator_enable -> use cnt: %d \n",mali_regulator_get_usecount()));
	mali_regulator_set_voltage(mali_gpu_vol, mali_gpu_vol);
#endif

	MALI_DEBUG_PRINT(2, ("MALI Clock is set at mali driver\n"));

	MALI_DEBUG_PRINT(3,
			("::clk_put:: %s mali_parent_clock - normal\n", __FUNCTION__));
	MALI_DEBUG_PRINT(3,
			("::clk_put:: %s mpll_clock  - normal\n", __FUNCTION__));

/*	if (mali_clock != NULL) {
		clk_put(mali_clock);
	}*/

	return MALI_TRUE;

#ifdef MALI_CFG_REGULATOR
	err_regulator:
	regulator_put(g3d_regulator);
#endif

	err_clock_get: 
/*	if (mali_clock != NULL) {
		clk_put(mali_clock);
	} */

	return ret;
}

mali_bool deinit_mali_clock(void) {
	if (mali_clock == 0)
		return MALI_TRUE;

#ifdef MALI_CFG_REGULATOR
	if (g3d_regulator)
	{
		regulator_put(g3d_regulator);
		g3d_regulator=NULL;
	}
#endif
	clk_set_rate(mali_clock, GPUMINCLK);
	clk_disable(mali_clock);
	if (mali_clock != NULL) {
		clk_put(mali_clock);
	}

	return MALI_TRUE;
}

/* Copyright (C) 2010, 2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef ARM_H_
#define ARM_H_
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/pm.h>
#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif
#include <asm/io.h>

#include "mach/irqs.h"
#include "mach/io.h"
#include "mach/pmu.h"


//#define CONFIG_MALI_OVERCLOCK_400 1



/* clock definitions */

#define PDGPUCLK_NAME 		"pd_gpu"
#define GPUCLK_NAME 		"gpu"
#define GPUMTO_NAME 		"codec_pll"


typedef struct mali_runtime_resumeTag {
	int clk;
	int vol;
} mali_runtime_resume_table;

mali_runtime_resume_table mali_runtime_resume = { 266, 900000 };

/* lock/unlock CPU freq by Mali */
extern int cpufreq_lock_by_mali(unsigned int freq);
extern void cpufreq_unlock_by_mali(void);

static struct clk *mali_clock = 0;

static unsigned int GPU_MHZ = 1000000;

#if defined(CONFIG_MALI_OVERCLOCK_400)
int mali_gpu_clk = 400;
int mali_gpu_vol = 1275000;
#else
int mali_gpu_clk = 266;
int mali_gpu_vol = 1050000;
#endif
#define GPUMINCLK 133000000
#if MALI_VOLTAGE_LOCK
int mali_lock_vol = 0;
static _mali_osk_atomic_t voltage_lock_status;
static mali_bool mali_vol_lock_flag = 0;
#endif

int gpu_power_state;
static int bPoweroff;

#ifdef MALI_CFG_REGULATOR
struct regulator {
	struct device *dev;
	struct list_head list;
	int uA_load;
	int min_uV;
	int max_uV;
	char *supply_name;
	struct device_attribute dev_attr;
	struct regulator_dev *rdev;
};

struct regulator *g3d_regulator=NULL;
#endif

mali_io_address clk_register_map = 0;

_mali_osk_lock_t *mali_dvfs_lock = 0;


static void mali_platform_device_release(struct device *device);

static struct platform_device mali_gpu_device = {
		.name = MALI_GPU_NAME_UTGARD,
		.id = 0,
		.dev.release = mali_platform_device_release
};

/* prototypes */

void mali_regulator_set_voltage(int min_uV, int max_uV);
mali_bool mali_clk_get(void);
mali_bool mali_clk_set_rate(unsigned int clk, unsigned int mhz);
mali_bool init_mali_clock(void);
mali_bool deinit_mali_clock(void);
_mali_osk_errcode_t gpu_power_domain_control(int bpower_on);
_mali_osk_errcode_t mali_platform_init(void);
_mali_osk_errcode_t mali_platform_powerdown(u32 cores);
_mali_osk_errcode_t mali_platform_powerup(u32 cores);
void mali_gpu_utilization_handler(u32 utilization);
u32 pmu_get_power_up_down_info(void);
int mali_voltage_lock_push(int lock_vol);
int mali_voltage_lock_pop(void);
int mali_voltage_lock_init(void);
int mali_platform_device_register(void);
void mali_platform_device_unregister(void);
void mali_platform_device_release(struct device *device);

#endif /* ARM_H_ */

#endif
