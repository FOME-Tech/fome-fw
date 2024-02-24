/**
 *
 * \file
 *
 * \brief This module contains NMC1000 M2M driver APIs implementation.
 *
 * Copyright (c) 2015 Atmel Corporation. All rights reserved.
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
#include "common/include/nm_common.h"
#include "driver/source/nmdrv.h"
#include "driver/source/nmasic.h"
#include "driver/source/nmbus.h"
#include "driver\source\nmasic.h"
#include "driver\source\nmbus.h"
#include "driver\include\m2m_types.h"



/**
*	@fn		nm_get_firmware_info(tstrM2mRev* M2mRev)
*	@brief	Get Firmware version info
*	@param [out]	M2mRev
*			    pointer holds address of structure "tstrM2mRev" that contains the firmware version parameters
*	@version	1.0
*/
sint8 nm_get_firmware_info(tstrM2mRev* M2mRev)
{
	uint16  curr_drv_ver, min_req_drv_ver,curr_firm_ver;
	uint32	reg = 0;
	sint8	ret = M2M_SUCCESS;

	ret = nm_read_reg_with_ret(NMI_REV_REG, &reg);

	M2mRev->u8DriverMajor	= M2M_GET_DRV_MAJOR(reg);
	M2mRev->u8DriverMinor   = M2M_GET_DRV_MINOR(reg);
	M2mRev->u8DriverPatch	= M2M_GET_DRV_PATCH(reg);
	M2mRev->u8FirmwareMajor	= M2M_GET_FW_MAJOR(reg);
	M2mRev->u8FirmwareMinor = M2M_GET_FW_MINOR(reg);
	M2mRev->u8FirmwarePatch = M2M_GET_FW_PATCH(reg);
	M2mRev->u32Chipid	= nmi_get_chipid();

	curr_firm_ver   = M2M_MAKE_VERSION(M2mRev->u8FirmwareMajor, M2mRev->u8FirmwareMinor,M2mRev->u8FirmwarePatch);
	curr_drv_ver    = M2M_MAKE_VERSION(M2M_DRIVER_VERSION_MAJOR_NO, M2M_DRIVER_VERSION_MINOR_NO, M2M_DRIVER_VERSION_PATCH_NO);
	min_req_drv_ver = M2M_MAKE_VERSION(M2mRev->u8DriverMajor, M2mRev->u8DriverMinor,M2mRev->u8DriverPatch);
	if(curr_drv_ver <  min_req_drv_ver) {
		/*The current driver version should be larger or equal
		than the min driver that the current firmware support  */
		ret = M2M_ERR_FW_VER_MISMATCH;
	}
	if(curr_drv_ver >  curr_firm_ver) {
		/*The current driver should be equal or less than the firmware version*/
		ret = M2M_ERR_FW_VER_MISMATCH;
	}
	return ret;
}
/**
*	@fn		nm_get_firmware_info(tstrM2mRev* M2mRev)
*	@brief	Get Firmware version info
*	@param [out]	M2mRev
*			    pointer holds address of structure "tstrM2mRev" that contains the firmware version parameters
*	@version	1.0
*/
sint8 nm_get_firmware_full_info(tstrM2mRev* pstrRev)
{
	uint16  curr_drv_ver, min_req_drv_ver,curr_firm_ver;
	uint32	reg = 0;
	sint8	ret = M2M_SUCCESS;
	tstrGpRegs strgp = {0};

	m2m_memset((uint8*)pstrRev,0,sizeof(tstrM2mRev));
	ret = nm_read_reg_with_ret(rNMI_GP_REG_2, &reg);
	if(ret == M2M_SUCCESS)
	{
		if(reg != 0)
		{
			ret = nm_read_block(reg|0x30000,(uint8*)&strgp,sizeof(tstrGpRegs));
			if(ret == M2M_SUCCESS)
			{
				reg = strgp.u32Firmware_Ota_rev;
				reg &= 0x0000ffff;
				if(reg != 0)
				{
					ret = nm_read_block(reg|0x30000,(uint8*)pstrRev,sizeof(tstrM2mRev));
					if(ret == M2M_SUCCESS)
					{
						curr_firm_ver   = M2M_MAKE_VERSION(pstrRev->u8FirmwareMajor, pstrRev->u8FirmwareMinor,pstrRev->u8FirmwarePatch);
						curr_drv_ver    = M2M_MAKE_VERSION(M2M_DRIVER_VERSION_MAJOR_NO, M2M_DRIVER_VERSION_MINOR_NO, M2M_DRIVER_VERSION_PATCH_NO);
						min_req_drv_ver = M2M_MAKE_VERSION(pstrRev->u8DriverMajor, pstrRev->u8DriverMinor,pstrRev->u8DriverPatch);
						if((curr_firm_ver == 0)||(min_req_drv_ver == 0)||(min_req_drv_ver == 0)){
							ret = M2M_ERR_FAIL;
							goto EXIT;
						}
						if(curr_drv_ver <  min_req_drv_ver) {
							/*The current driver version should be larger or equal 
							than the min driver that the current firmware support  */
							ret = M2M_ERR_FW_VER_MISMATCH;
							goto EXIT;
						}
						if(curr_drv_ver >  curr_firm_ver) {
							/*The current driver should be equal or less than the firmware version*/
							ret = M2M_ERR_FW_VER_MISMATCH;
							goto EXIT;
						}
					}
				}else {
					ret = M2M_ERR_FAIL;
				}
			}
		}else{
			ret = M2M_ERR_FAIL;
		}
	}
EXIT:
	return ret;
}
/**
*	@fn		nm_get_ota_firmware_info(tstrM2mRev* pstrRev)
*	@brief	Get Firmware version info
*	@param [out]	M2mRev
*			    pointer holds address of structure "tstrM2mRev" that contains the firmware version parameters
			
*	@version	1.0
*/
sint8 nm_get_ota_firmware_info(tstrM2mRev* pstrRev)
{
	uint16  curr_drv_ver, min_req_drv_ver,curr_firm_ver;
	uint32	reg = 0;
	sint8	ret;
	tstrGpRegs strgp = {0};

	m2m_memset((uint8*)pstrRev,0,sizeof(tstrM2mRev));
	ret = nm_read_reg_with_ret(rNMI_GP_REG_2, &reg);
	if(ret == M2M_SUCCESS)
	{
		if(reg != 0)
		{
			ret = nm_read_block(reg|0x30000,(uint8*)&strgp,sizeof(tstrGpRegs));
			if(ret == M2M_SUCCESS)
			{
				reg = strgp.u32Firmware_Ota_rev;
				reg >>= 16;
				if(reg != 0)
				{
					ret = nm_read_block(reg|0x30000,(uint8*)pstrRev,sizeof(tstrM2mRev));
					if(ret == M2M_SUCCESS)
					{
						curr_firm_ver   = M2M_MAKE_VERSION(pstrRev->u8FirmwareMajor, pstrRev->u8FirmwareMinor,pstrRev->u8FirmwarePatch);
						curr_drv_ver    = M2M_MAKE_VERSION(M2M_DRIVER_VERSION_MAJOR_NO, M2M_DRIVER_VERSION_MINOR_NO, M2M_DRIVER_VERSION_PATCH_NO);
						min_req_drv_ver = M2M_MAKE_VERSION(pstrRev->u8DriverMajor, pstrRev->u8DriverMinor,pstrRev->u8DriverPatch);
						if((curr_firm_ver == 0)||(min_req_drv_ver == 0)||(min_req_drv_ver == 0)){
							ret = M2M_ERR_FAIL;
							goto EXIT;
						}
						if(curr_drv_ver <  min_req_drv_ver) {
							/*The current driver version should be larger or equal 
							than the min driver that the current firmware support  */
							ret = M2M_ERR_FW_VER_MISMATCH;
						}
						if(curr_drv_ver >  curr_firm_ver) {
							/*The current driver should be equal or less than the firmware version*/
							ret = M2M_ERR_FW_VER_MISMATCH;
						}
					}
				}else{
					ret = M2M_ERR_FAIL;
				}
			}
		}else{
			ret = M2M_ERR_FAIL;
		}
	}
EXIT:
	return ret;
}

/*
*	@fn		nm_drv_init
*	@brief	Initialize NMC1000 driver
*	@return	M2M_SUCCESS in case of success and Negative error code in case of failure
*   @param [in]	arg
*				Generic argument
*	@author	M. Abdelmawla
*	@date	15 July 2012
*	@version	1.0
*/
sint8 nm_drv_init(void * arg)
{
	sint8 ret = M2M_SUCCESS;
	tstrM2mRev strtmp;

	if (REV(nmi_get_chipid()) == REV_3A0) {
		chip_apply_conf(rHAVE_USE_PMU_BIT|rHAVE_CHANNEL_INDEX_ONE);
	} else {
		chip_apply_conf(rHAVE_CHANNEL_INDEX_ONE);
	}

	ret = nm_get_firmware_full_info(&strtmp);

	M2M_INFO("Firmware ver   : %u.%u.%u Svnrev %u\n", strtmp.u8FirmwareMajor, strtmp.u8FirmwareMinor, strtmp.u8FirmwarePatch,strtmp.u16FirmwareSvnNum);
	M2M_INFO("Min driver ver : %u.%u.%u\n", strtmp.u8DriverMajor, strtmp.u8DriverMinor, strtmp.u8DriverPatch);
	M2M_INFO("Curr driver ver: %u.%u.%u Svnrev %u\n", M2M_DRIVER_VERSION_MAJOR_NO, M2M_DRIVER_VERSION_MINOR_NO, M2M_DRIVER_VERSION_PATCH_NO,M2M_DRIVER_SVN_VERSION);

	if(M2M_ERR_FW_VER_MISMATCH == ret)
	{
		ret = M2M_ERR_FW_VER_MISMATCH;
		M2M_ERR("Mismatch Firmware Version\n");
	}

	return ret;
}

/*
*	@fn		nm_drv_deinit
*	@brief	Deinitialize NMC1000 driver
*	@author	M. Abdelmawla
*	@date	17 July 2012
*	@version	1.0
*/
sint8 nm_drv_deinit(void * arg)
{

	return M2M_SUCCESS;
}
#endif
