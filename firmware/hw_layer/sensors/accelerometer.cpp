/*
 * @file accelerometer.cpp
 *
 * stm32f4discovery has MEMS LIS302DL
 * www.st.com/resource/en/datasheet/lis302dl.pdf
 *
 * SPI1
 * LIS302DL_SPI_SCK PA5
 * LIS302DL_SPI_MISO PA6
 * LIS302DL_SPI_MOSI PA7
 * LIS302DL_SPI_CS_PIN PE3
 *
 *
 *
 * @date May 19, 2016
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "accelerometer.h"
#include "hardware.h"

#if EFI_MEMS
#include "mpu_util.h"
#include "lis302dl.h"
#include "periodic_thread_controller.h"

static SPIDriver *driver;

/*
 * SPI1 configuration structure.
 * Speed 5.25MHz, CPHA=1, CPOL=1, 8bits frames, MSb transmitted first.
 * The slave select line is the pin GPIOE_CS_SPI on the port GPIOE.
 */
static const SPIConfig accelerometerCfg = {
	.spi_bus = NULL,
	/* HW dependent part.*/
	.ssport = GPIOE,
	.sspad = GPIOE_PIN3,
	.cr1 = SPI_CR1_BR_0 | SPI_CR1_BR_1 | SPI_CR1_CPOL | SPI_CR1_CPHA |
		SPI_CR1_8BIT_MODE,
	.cr2 = SPI_CR2_8BIT_MODE
};
#endif /* EFI_MEMS */

#if EFI_MEMS

static THD_WORKING_AREA(ivThreadStack, UTILITY_THREAD_STACK_SIZE);

class AccelController : public PeriodicController<UTILITY_THREAD_STACK_SIZE> {
public:
	AccelController() : PeriodicController("Acc SPI") { }
private:
	void PeriodicTask(efitick_t nowNt) override	{
		// has to be a thread since we want to use blocking method - blocking method only available in threads, not in interrupt handler
		// todo: migrate to async SPI API?
		engine->sensors.accelerometer.x = (int8_t)lis302dlReadRegister(driver, LIS302DL_OUTX);
		engine->sensors.accelerometer.y = (int8_t)lis302dlReadRegister(driver, LIS302DL_OUTY);
		chThdSleepMilliseconds(20);
	}
};

static BenchController instance;

void initAccelerometer() {
	if (!isBrainPinValid(engineConfiguration->LIS302DLCsPin))
		return; // not used

	if (!engineConfiguration->is_enabled_spi_1)
		return; // temporary
#if HAL_USE_SPI
	driver = getSpiDevice(engineConfiguration->accelerometerSpiDevice);
	if (driver == NULL) {
		// error already reported
		return;
	}

	spiStart(driver, &accelerometerCfg);
	initSpiCs((SPIConfig *)driver->config, engineConfiguration->LIS302DLCsPin);

//	memsCs.initPin("LIS302 CS", engineConfiguration->LIS302DLCsPin);
//	memsCfg.ssport = getHwPort("mmc", engineConfiguration->sdCardCsPin);
//	memsCfg.sspad = getHwPin("mmc", engineConfiguration->sdCardCsPin);


	/* LIS302DL initialization.*/
	lis302dlWriteRegister(driver, LIS302DL_CTRL_REG1, 0x47); // enable device, enable XYZ
	lis302dlWriteRegister(driver, LIS302DL_CTRL_REG2, 0x00); // 4 wire mode
	lis302dlWriteRegister(driver, LIS302DL_CTRL_REG3, 0x00);

	chThdCreateStatic(ivThreadStack, sizeof(ivThreadStack), NORMALPRIO, (tfunc_t)(void*) ivThread, NULL);
#endif /* HAL_USE_SPI */
}

#endif /* EFI_MEMS */
