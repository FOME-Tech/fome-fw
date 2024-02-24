/**
 *
 * \file
 *
 * \brief This module contains NMC1000 bus APIs implementation.
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
#include "nmbus.h"

#define NM_READ_REG(x)			(*(volatile unsigned int*)(0x30000000ul+x))
#define NM_WRITE_REG(x,val)		(*(volatile unsigned int*)(0x30000000ul+x) = val)

/**
 *	@fn		nm_bus_iface_init
 *	@brief	Initialize bus interface
 *	@return	M2M_SUCCESS in case of success and M2M_ERR_BUS_FAIL in case of failure
 *	@author	M. Abdelmawla
 *	@date	11 July 2012
 *	@version	1.0
 */
sint8 nm_bus_iface_init(void *pvInitVal) {
	sint8 ret = M2M_SUCCESS;
	return ret;
}

/**
 *	@fn		nm_bus_iface_deinit
 *	@brief	Deinitialize bus interface
 *	@return	M2M_SUCCESS in case of success and M2M_ERR_BUS_FAIL in case of failure
 *	@author	Samer Sarhan
 *	@date	07 April 2014
 *	@version	1.0
 */
sint8 nm_bus_iface_deinit(void) {
	sint8 ret = M2M_SUCCESS;

	return ret;
}

/**
 *	@fn		nm_bus_iface_reconfigure
 *	@brief	reconfigure bus interface
 *	@return	M2M_SUCCESS in case of success and M2M_ERR_BUS_FAIL in case of failure
 *	@author	Viswanathan Murugesan
 *	@date	22 Oct 2014
 *	@version	1.0
 */
sint8 nm_bus_iface_reconfigure(void *ptr) {
	sint8 ret = M2M_SUCCESS;

	return ret;
}
/*
 *	@fn		nm_read_reg
 *	@brief	Read register
 *	@param [in]	u32Addr
 *				Register address
 *	@return	Register value
 *	@author	M. Abdelmawla
 *	@date	11 July 2012
 *	@version	1.0
 */
uint32 nm_read_reg(uint32 u32Addr) {
	return NM_READ_REG(u32Addr);
}

/*
 *	@fn		nm_read_reg_with_ret
 *	@brief	Read register with error code return
 *	@param [in]	u32Addr
 *				Register address
 *	@param [out]	pu32RetVal
 *				Pointer to u32 variable used to return the read value
 *	@return	M2M_SUCCESS in case of success and M2M_ERR_BUS_FAIL in case of failure
 *	@author	M. Abdelmawla
 *	@date	11 July 2012
 *	@version	1.0
 */
sint8 nm_read_reg_with_ret(uint32 u32Addr, uint32* pu32RetVal) {
	*pu32RetVal = NM_READ_REG(u32Addr);
	return M2M_SUCCESS;
}

/*
 *	@fn		nm_write_reg
 *	@brief	write register
 *	@param [in]	u32Addr
 *				Register address
 *	@param [in]	u32Val
 *				Value to be written to the register
 *	@return	M2M_SUCCESS in case of success and M2M_ERR_BUS_FAIL in case of failure
 *	@author	M. Abdelmawla
 *	@date	11 July 2012
 *	@version	1.0
 */
sint8 nm_write_reg(uint32 u32Addr, uint32 u32Val) {
	NM_WRITE_REG(u32Addr, u32Val);
	return M2M_SUCCESS;
}
/*
 *	@fn		nm_read_block
 *	@brief	Read block of data
 *	@param [in]	u32Addr
 *				Start address
 *	@param [out]	puBuf
 *				Pointer to a buffer used to return the read data
 *	@param [in]	u32Sz
 *				Number of bytes to read. The buffer size must be >= u32Sz
 *	@return	M2M_SUCCESS in case of success and M2M_ERR_BUS_FAIL in case of failure
 *	@author	M. Abdelmawla
 *	@date	11 July 2012
 *	@version	1.0
 */
sint8 nm_read_block(uint32 u32Addr, uint8 *puBuf, uint32 u32Sz) {
	sint8 ret = M2M_ERR_FAIL;
	uint8 * pu8Mem = ((uint8*) (0x30000000ul + u32Addr));
	if ((u32Sz) && (puBuf != NULL)) {
		m2m_memcpy(puBuf, pu8Mem, u32Sz);
		ret = M2M_SUCCESS;
	}
	return ret;
}

/**
 *	@fn		nm_write_block
 *	@brief	Write block of data
 *	@param [in]	u32Addr
 *				Start address
 *	@param [in]	puBuf
 *				Pointer to the buffer holding the data to be written
 *	@param [in]	u32Sz
 *				Number of bytes to write. The buffer size must be >= u32Sz
 *	@return	M2M_SUCCESS in case of success and M2M_ERR_BUS_FAIL in case of failure
 *	@author	M. Abdelmawla
 *	@date	11 July 2012
 *	@version	1.0
 */
sint8 nm_write_block(uint32 u32Addr, uint8 *puBuf, uint32 u32Sz) {
	sint8 ret = M2M_ERR_FAIL;
	uint8 * pu8Mem = ((uint8*) (0x30000000ul + u32Addr));
	if ((u32Sz) && (puBuf != NULL)) {
		m2m_memcpy(pu8Mem, puBuf, u32Sz);
		ret = M2M_SUCCESS;
	}
	return ret;
}
#endif /* CORTUS_APP */

