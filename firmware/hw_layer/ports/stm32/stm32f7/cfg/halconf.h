/*
    ChibiOS - Copyright (C) 2006..2020 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/**
 * @file    templates/halconf.h
 * @brief   HAL configuration header.
 * @details HAL configuration file, this file allows to enable or disable the
 *          various device drivers from your application. You may also use
 *          this file in order to override the device drivers default settings.
 *
 * @addtogroup HAL_CONF
 * @{
 */

#ifndef HALCONF_H
#define HALCONF_H

#define _CHIBIOS_HAL_CONF_
#define _CHIBIOS_HAL_CONF_VER_8_4_

#include "mcuconf.h"

#include "rusefi_halconf.h"

/**
 * @brief   Enables the cryptographic subsystem.
 */
#if !defined(HAL_USE_CRY) || defined(__DOXYGEN__)
#define HAL_USE_CRY                         FALSE
#endif

/**
 * @brief   Enables the DAC subsystem.
 */
#if !defined(HAL_USE_DAC) || defined(__DOXYGEN__)
#define HAL_USE_DAC                 FALSE
#endif

/**
 * @brief   Enables the EFlash subsystem.
 */
#if !defined(HAL_USE_EFL) || defined(__DOXYGEN__)
#define HAL_USE_EFL                         FALSE
#endif

/**
 * @brief   Enables the I2C subsystem.
 */
#if !defined(HAL_USE_I2C) || defined(__DOXYGEN__)
#define HAL_USE_I2C                 FALSE
#endif

// /**
//  * @brief   Enables the I2S subsystem.
//  */
// #if !defined(HAL_USE_I2S) || defined(__DOXYGEN__)
// #define HAL_USE_I2S                         FALSE
// #endif

/**
 * @brief   Enables the ICU subsystem.
 */
#if !defined(HAL_USE_ICU) || defined(__DOXYGEN__)
#define HAL_USE_ICU                 TRUE
#endif

/**
 * @brief   Enables the PWM subsystem.
 */
#if !defined(HAL_USE_PWM) || defined(__DOXYGEN__)
#define HAL_USE_PWM                 TRUE
#endif

/**
 * @brief   Enables the RTC subsystem.
 */
#if !defined(HAL_USE_RTC) || defined(__DOXYGEN__)
#define HAL_USE_RTC                 TRUE
#endif

/**
 * @brief   Enables the SDC subsystem.
 */
#if !defined(HAL_USE_SDC) || defined(__DOXYGEN__)
#define HAL_USE_SDC                 FALSE
#endif

/**
 * @brief   Enables the SERIAL subsystem.
 */
#if !defined(HAL_USE_SERIAL) || defined(__DOXYGEN__)
#define HAL_USE_SERIAL              TRUE
#endif

/**
 * @brief   Enables the SIO subsystem.
 */
#if !defined(HAL_USE_SIO) || defined(__DOXYGEN__)
#define HAL_USE_SIO                         FALSE
#endif

/**
 * @brief   Enables the SPI subsystem.
 */
#if !defined(HAL_USE_SPI) || defined(__DOXYGEN__)
#define HAL_USE_SPI                 TRUE
#endif

/**
 * @brief   Enables the TRNG subsystem.
 */
#if !defined(HAL_USE_TRNG) || defined(__DOXYGEN__)
#define HAL_USE_TRNG                FALSE
#endif

/**
 * @brief   Enables the UART subsystem.
 */
#if !defined(HAL_USE_UART) || defined(__DOXYGEN__)
/* Configured in efifeatures.h */
#if defined(TS_PRIMARY_UxART_PORT)
#define HAL_USE_UART                TRUE
#else
#define HAL_USE_UART                FALSE
#endif
#endif

/**
 * @brief   Enables the WDG subsystem.
 */
#if !defined(HAL_USE_WDG) || defined(__DOXYGEN__)
#define HAL_USE_WDG                 FALSE
#endif

/**
 * @brief   Enables the WSPI subsystem.
 */
#if !defined(HAL_USE_WSPI) || defined(__DOXYGEN__)
#define HAL_USE_WSPI                        FALSE
#endif

/*===========================================================================*/
/* PAL driver related settings.                                              */
/*===========================================================================*/

/**
 * @brief   Enables synchronous APIs.
 * @note    Disabling this option saves both code and data space.
 */
#if !defined(PAL_USE_WAIT) || defined(__DOXYGEN__)
#define PAL_USE_WAIT                        FALSE
#endif

/*===========================================================================*/
/* CAN driver related settings.                                              */
/*===========================================================================*/

/**
 * @brief   Enforces the driver to use direct callbacks rather than OSAL events.
 */
#if !defined(CAN_ENFORCE_USE_CALLBACKS) || defined(__DOXYGEN__)
#define CAN_ENFORCE_USE_CALLBACKS           FALSE
#endif

/*===========================================================================*/
/* I2C driver related settings.                                              */
/*===========================================================================*/

/**
 * @brief   Enables the mutual exclusion APIs on the I2C bus.
 */
#if !defined(I2C_USE_MUTUAL_EXCLUSION) || defined(__DOXYGEN__)
#define I2C_USE_MUTUAL_EXCLUSION            TRUE
#endif

/*===========================================================================*/
/* SDC driver related settings.                                              */
/*===========================================================================*/

/**
 * @brief   Number of initialization attempts before rejecting the card.
 * @note    Attempts are performed at 10mS intervals.
 */
#if !defined(SDC_INIT_RETRY) || defined(__DOXYGEN__)
#define SDC_INIT_RETRY              100
#endif

/**
 * @brief   Include support for MMC cards.
 * @note    MMC support is not yet implemented so this option must be kept
 *          at @p FALSE.
 */
#if !defined(SDC_MMC_SUPPORT) || defined(__DOXYGEN__)
#define SDC_MMC_SUPPORT             FALSE
#endif

/**
 * @brief   Delays insertions.
 * @details If enabled this options inserts delays into the MMC waiting
 *          routines releasing some extra CPU time for the threads with
 *          lower priority, this may slow down the driver a bit however.
 */
#if !defined(SDC_NICE_WAITING) || defined(__DOXYGEN__)
#define SDC_NICE_WAITING            TRUE
#endif

/**
 * @brief   OCR initialization constant for V20 cards.
 */
#if !defined(SDC_INIT_OCR_V20) || defined(__DOXYGEN__)
#define SDC_INIT_OCR_V20                    0x50FF8000U
#endif

/**
 * @brief   OCR initialization constant for non-V20 cards.
 */
#if !defined(SDC_INIT_OCR) || defined(__DOXYGEN__)
#define SDC_INIT_OCR                        0x80100000U
#endif

/*===========================================================================*/
/* SERIAL driver related settings.                                           */
/*===========================================================================*/

/**
 * @brief   Default bit rate.
 * @details Configuration parameter, this is the baud rate selected for the
 *          default configuration.
 */
#if !defined(SERIAL_DEFAULT_BITRATE) || defined(__DOXYGEN__)
#define SERIAL_DEFAULT_BITRATE      38400
#endif

/**
 * @brief   Serial buffers size.
 * @details Configuration parameter, you can change the depth of the queue
 *          buffers depending on the requirements of your application.
 * @note    The default is 16 bytes for both the transmission and receive
 *          buffers.
 */
#if !defined(SERIAL_BUFFERS_SIZE) || defined(__DOXYGEN__)
#define SERIAL_BUFFERS_SIZE         16
#endif

/*===========================================================================*/
/* SPI driver related settings.                                              */
/*===========================================================================*/

/**
 * @brief   Inserts an assertion on function errors before returning.
 */
#if !defined(SPI_USE_ASSERT_ON_ERROR) || defined(__DOXYGEN__)
#define SPI_USE_ASSERT_ON_ERROR             TRUE
#endif

/**
 * @brief   Handling method for SPI CS line.
 * @note    Disabling this option saves both code and data space.
 */
#if !defined(SPI_SELECT_MODE) || defined(__DOXYGEN__)
#define SPI_SELECT_MODE                     SPI_SELECT_MODE_PAD
#endif

/*===========================================================================*/
/* UART driver related settings.                                             */
/*===========================================================================*/

/**
 * @brief   Enables synchronous APIs.
 * @note    Disabling this option saves both code and data space.
 */
#if !defined(UART_USE_WAIT) || defined(__DOXYGEN__)
#define UART_USE_WAIT               TRUE
#endif

/**
 * @brief   Enables the @p uartAcquireBus() and @p uartReleaseBus() APIs.
 * @note    Disabling this option saves both code and data space.
 */
#if !defined(UART_USE_MUTUAL_EXCLUSION) || defined(__DOXYGEN__)
#define UART_USE_MUTUAL_EXCLUSION   FALSE
#endif

#endif /* HALCONF_H */

/** @} */
