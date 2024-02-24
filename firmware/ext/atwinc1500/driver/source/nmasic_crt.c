/**
 *
 * \file
 *
 * \brief This module contains NMC1500 ASIC specific internal APIs.
 *
 * Copyright (c) 2014 Atmel Corporation. All rights reserved.
 *
 * \asf_license_start
 *
 * \page License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. The name of Atmel may not be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. This software may only be redistributed and used in connection with an
 *    Atmel microcontroller product.
 *
 * THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * EXPRESSLY AND SPECIFICALLY DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \asf_license_stop
 *
 */
#ifdef CORTUS_APP
#include "common\include\nm_common.h"
#include "driver\source\nmbus.h"
#include "bsp\include\nm_bsp.h"
#include "driver\source\nmasic.h"

#define NMI_GLB_RESET_0				(NMI_PERIPH_REG_BASE + 0x400)

sint8 chip_apply_conf(uint32 u32Conf)
{
	sint8 ret = M2M_SUCCESS;
	uint32 val32 = u32Conf;
	
#if (defined  __ENABLE_PMU__ ) || (defined CONF_WINC_INT_PMU)
	val32 |= rHAVE_USE_PMU_BIT;
#endif
#ifdef __ENABLE_SLEEP_CLK_SRC_RTC__
	val32 |= rHAVE_SLEEP_CLK_SRC_RTC_BIT;
#elif defined __ENABLE_SLEEP_CLK_SRC_XO__
	val32 |= rHAVE_SLEEP_CLK_SRC_XO_BIT;
#endif
#ifdef __ENABLE_EXT_PA_INV_TX_RX__
	val32 |= rHAVE_EXT_PA_INV_TX_RX;
#endif
#ifdef __ENABLE_LEGACY_RF_SETTINGS__
	val32 |= rHAVE_LEGACY_RF_SETTINGS;
#endif
#ifdef __DISABLE_FIRMWARE_LOGS__
	val32 |= rHAVE_LOGS_DISABLED_BIT;
#endif
	val32 |= rHAVE_RESERVED1_BIT;
	do  {
		nm_write_reg(rNMI_GP_REG_1, val32);
		if(val32 != 0) {		
			uint32 reg = 0;
			ret = nm_read_reg_with_ret(rNMI_GP_REG_1, &reg);
			if(ret == M2M_SUCCESS) {
				if(reg == val32)
					break;
			}
		} else {
			break;
		}
	} while(1);

	return M2M_SUCCESS;
}
sint8 nm_clkless_wake(void)
{
	return M2M_ERR_FAIL;
}
void chip_idle(void)
{

}

void enable_rf_blocks(void)
{

}

sint8 enable_interrupts(void) 
{
	return M2M_ERR_FAIL;
}

sint8 cpu_start(void) 
{
	return M2M_ERR_FAIL;
}

uint32 nmi_get_chipid(void)
{
	static uint32 chipid = 0;

	if (chipid == 0) {
		//uint32 revid;
		uint32 rfrevid;

		if((nm_read_reg_with_ret(0x1000, &chipid)) != M2M_SUCCESS) {
			chipid = 0;
			return 0;
		}
		//if((ret = nm_read_reg_with_ret(0x11fc, &revid)) != M2M_SUCCESS) {
		//	return 0;
		//}
		if((nm_read_reg_with_ret(0x13f4, &rfrevid)) != M2M_SUCCESS) {
			chipid = 0;
			return 0;
		}

		if (chipid == 0x1002a0)  {
			if (rfrevid == 0x1) { /* 1002A0 */
			} else /* if (rfrevid == 0x2) */ { /* 1002A1 */
				chipid = 0x1002a1;
			}
		} else if(chipid == 0x1002b0) {
			if(rfrevid == 3) { /* 1002B0 */
			} else if(rfrevid == 4) { /* 1002B1 */
				chipid = 0x1002b1;
			} else /* if(rfrevid == 5) */ { /* 1002B2 */
				chipid = 0x1002b2;
			}
		} else {
		}
//#define PROBE_FLASH
#ifdef PROBE_FLASH
		if(chipid) {
			UWORD32 flashid;

			flashid = probe_spi_flash();
			if(flashid == 0x1230ef) {
				chipid &= ~(0x0f0000);
				chipid |= 0x050000;
			}
			if(flashid == 0xc21320c2) {
				chipid &= ~(0x0f0000);
				chipid |= 0x050000;
			}
		}
#else
		/*M2M is by default have SPI flash*/
		chipid &= ~(0x0f0000);
		chipid |= 0x050000;
#endif /* PROBE_FLASH */
	}
	return chipid;
}

uint32 nmi_get_rfrevid(void)
{
    uint32 rfrevid;
    if((nm_read_reg_with_ret(0x13f4, &rfrevid)) != M2M_SUCCESS) {
        rfrevid = 0;
        return 0;
    }
    return rfrevid;
}

void restore_pmu_settings_after_global_reset(void)
{

}

void nmi_update_pll(void)
{

}
void nmi_set_sys_clk_src_to_xo(void) 
{

}
sint8 chip_wake(void)
{

	return M2M_ERR_FAIL;
}
sint8 chip_reset_and_cpu_halt(void)
{
	return M2M_ERR_FAIL;
}
sint8 chip_reset(void)
{
	uint32 val = 0;
	val = nm_read_reg(NMI_GLB_RESET_0);
	nm_write_reg(NMI_GLB_RESET_0, val & (~NBIT0));
	return M2M_SUCCESS;
}

sint8 wait_for_bootrom(uint8 arg)
{

	return M2M_ERR_FAIL;
}

sint8 wait_for_firmware_start(uint8 arg)
{

	return M2M_ERR_FAIL;
}

sint8 chip_deinit(void)
{
	return M2M_ERR_FAIL;
}

#ifdef CONF_PERIPH

sint8 set_gpio_dir(uint8 gpio, uint8 dir)
{
	return M2M_ERR_FAIL;
}
sint8 set_gpio_val(uint8 gpio, uint8 val)
{
	return M2M_ERR_FAIL;
}

sint8 get_gpio_val(uint8 gpio, uint8* val)
{

	return M2M_ERR_FAIL;
}

sint8 pullup_ctrl(uint32 pinmask, uint8 enable)
{
	return M2M_ERR_FAIL;
}
#endif /* CONF_PERIPH */

sint8 nmi_get_otp_mac_address(uint8 *pu8MacAddr,  uint8 * pu8IsValid)
{
	sint8 ret;
	uint32	u32RegValue;
	uint8	mac[6];
	tstrGpRegs strgp = {0};

	ret = nm_read_reg_with_ret(rNMI_GP_REG_2, &u32RegValue);
	if(ret != M2M_SUCCESS) goto _EXIT_ERR;

	ret = nm_read_block(u32RegValue|0x30000,(uint8*)&strgp,sizeof(tstrGpRegs));
	if(ret != M2M_SUCCESS) goto _EXIT_ERR;
	u32RegValue = strgp.u32Mac_efuse_mib;

	if(!EFUSED_MAC(u32RegValue)) {
		M2M_DBG("Default MAC\n");
		m2m_memset(pu8MacAddr, 0, 6);
		goto _EXIT_ERR;
	}

	M2M_DBG("OTP MAC\n");
	u32RegValue >>=16;
	ret = nm_read_block(u32RegValue|0x30000, mac, 6);
	m2m_memcpy(pu8MacAddr,mac,6);
	if(pu8IsValid) *pu8IsValid = 1;
	return ret;

_EXIT_ERR:
	if(pu8IsValid) *pu8IsValid = 0;
	return ret;
}

sint8 nmi_get_mac_address(uint8 *pu8MacAddr)
{
	sint8 ret;
	uint32	u32RegValue;
	uint8	mac[6];
	tstrGpRegs strgp = {0};

	ret = nm_read_reg_with_ret(rNMI_GP_REG_2, &u32RegValue);
	if(ret != M2M_SUCCESS) goto _EXIT_ERR;

	ret = nm_read_block(u32RegValue|0x30000,(uint8*)&strgp,sizeof(tstrGpRegs));
	if(ret != M2M_SUCCESS) goto _EXIT_ERR;
	u32RegValue = strgp.u32Mac_efuse_mib;

	u32RegValue &=0x0000ffff;
	ret = nm_read_block(u32RegValue|0x30000, mac, 6);
	m2m_memcpy(pu8MacAddr, mac, 6);

	return ret;

_EXIT_ERR:
	return ret;
}
#endif /* CORTUS_APP */
