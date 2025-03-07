/*
 * hellen_meta.h
 *
 * @date Oct 2, 2021
 * @author Andrey Belomutskiy, (c) 2012-2021
 */

#pragma once

// see https://github.com/andreika-git/hellen-one/blob/master/board_id/board_ids.csv
#define BOARD_ID_154hyundai_c 10
#define BOARD_ID_154hyundai_d 201

#define BOARD_ID_2chan_b 3
#define BOARD_ID_2chan_c 5
#define BOARD_ID_2chan_d 107
// mega-module starts here
#define BOARD_ID_2chan_e 112
#define BOARD_ID_2chan_f 115
#define BOARD_ID_2chan_g 210

#define BOARD_ID_4chan_d 103
#define BOARD_ID_4chan_e 105
#define BOARD_ID_4chan_f 113
#define BOARD_ID_4chan_g 203
#define BOARD_ID_4chan_h 114

void hellenWbo();

void setHellenMegaEnPin();
void setHellenEnPin(Gpio pin);
void setHellen64MegaEnPin();
void hellenBoardStandBy();
void configureHellenMegaAccCS2Pin();
void configureHellenCanTerminator();

void setHellenCan();
void setHellen64Can();

int detectHellenBoardId();
void detectHellenBoardType();

#define H64_LED1_RED Gpio::B0
#define H64_LED2_BLUE Gpio::B1

#define H64_IO1 Gpio::C13
#define H64_SPI1_CS1 Gpio::D2

#define H64_OUT_PWM1 Gpio::B7
#define H64_OUT_PWM2 Gpio::C8
#define H64_OUT_PWM3 Gpio::C9
#define H64_OUT_PWM4 Gpio::A8
#define H64_OUT_PWM5 Gpio::A9
#define H64_OUT_PWM6 Gpio::A15

// stm32 UART8
#define H144_UART1_RX Gpio::E0
#define H144_UART1_TX Gpio::E1

// stm32 UART2
#define H144_UART2_RX Gpio::D6
#define H144_UART2_TX Gpio::D5

#define H144_LED1_RED Gpio::G0
#define H144_LED2 Gpio::G1
#define H144_LED3 Gpio::E7
#define H144_LED4 Gpio::E8

#define H144_USB1ID Gpio::A10

#define H176_LED1_RED Gpio::H8

#define H144_LS_1 Gpio::G7
#define H144_LS_2 Gpio::G8
#define H144_LS_3 Gpio::D11
#define H144_LS_4 Gpio::D10
#define H144_LS_5 Gpio::D9
#define H144_LS_6 Gpio::F12
#define H144_LS_7 Gpio::F13
#define H144_LS_8 Gpio::F14

#define H144_IGN_1 Gpio::C13
#define H144_IGN_2 Gpio::E5
#define H144_IGN_3 Gpio::E4
#define H144_IGN_4 Gpio::E3
#define H144_IGN_5 Gpio::E2
#define H144_IGN_6 Gpio::B8
#define H144_IGN_7 Gpio::B9
#define H144_IGN_8 Gpio::E6

#define H176_IGN_1 Gpio::I8
#define H176_IGN_2 Gpio::E5
#define H176_IGN_3 Gpio::E4
#define H176_IGN_4 Gpio::E3
#define H176_IGN_5 Gpio::E2

#define H144_OUT_PWM1 Gpio::D13
#define H144_OUT_PWM2 Gpio::C6
#define H144_OUT_PWM3 Gpio::C7
#define H144_OUT_PWM4 Gpio::C8
#define H144_OUT_PWM5 Gpio::C9
#define H144_OUT_PWM6 Gpio::D14
#define H144_OUT_PWM7 Gpio::D15
#define H144_OUT_PWM8 Gpio::D12


// also IO1/IO5 on rev 0.6
#define H144_GP1 Gpio::D4
// also IO2/IO5 on rev 0.6
#define H144_GP2 Gpio::D7
#define H144_GP3 Gpio::G10
#define H144_GP4 Gpio::G9
#define H144_GP5 Gpio::G15
#define H144_GP6 Gpio::D8
#define H144_GP7 Gpio::F15
#define H144_GP8 Gpio::E10
#define H144_GP9 Gpio::F2

#define H144_OUT_IO1 Gpio::D3
#define H144_OUT_IO2 Gpio::A9
#define H144_OUT_IO3 Gpio::G14
#define H144_OUT_IO4 Gpio::G5
#define H144_OUT_IO5 Gpio::D2
#define H144_OUT_IO6 Gpio::G11
#define H144_OUT_IO7 Gpio::G3
#define H144_OUT_IO8 Gpio::G4
#define H144_OUT_IO9 Gpio::G13
#define H144_OUT_IO10 Gpio::G12
#define H144_OUT_IO11 Gpio::G2
#define H144_OUT_IO12 Gpio::A8
#define H144_OUT_IO13 Gpio::G6

// A24
#define H144_IN_CRANK Gpio::B1
#define H144_IN_CRANK_ANALOG EFI_ADC_9
// A19
#define H144_IN_CAM Gpio::A6
#define H144_IN_CAM_ANALOG EFI_ADC_6
// DIN5
#define H144_IN_VSS Gpio::F11

// IN_CLT AIN11 PC2
#define H144_IN_CLT EFI_ADC_12
// IN_IAT AIN14 PC3
#define H144_IN_IAT EFI_ADC_13

// IN_PPS AIN18 PA3
#define H144_IN_PPS EFI_ADC_3
// IN_TPS AIN17 PA4
#define H144_IN_TPS EFI_ADC_4
// VIGN PA5
#define H144_IN_VBATT EFI_ADC_5

// muxed
#define H144_IN_TPS2 EFI_ADC_20

// IN_MAP1 AIN9 PC0
#define H144_IN_MAP1 EFI_ADC_10
// AIN10 PC1
#define H144_IN_MAP2 EFI_ADC_11
// AIN15 A15 PA2
#define H144_IN_MAP3 EFI_ADC_2
#define H144_IN_MAP3_DIGITAL Gpio::A2

// AIN7
#define H144_IN_RES1 Gpio::F9

// AIN16
#define H144_IN_RES2 Gpio::F10
#define H144_ADC_IN_RES2 ADC_CHANNEL_IN8

// AIN8
#define H144_IN_RES3 Gpio::F8
#define H144_ADC_IN_RES3 ADC_CHANNEL_IN6


#define H144_IN_AUX1 EFI_ADC_8
//
#define H144_IN_AUX2 EFI_ADC_14

// AIN22 PC5
#define H144_IN_AUX3 EFI_ADC_15
// AIN20 PA7
#define H144_IN_AUX4 EFI_ADC_7

// A1 AIN1 no code to use ADC3 as analog
#define H144_IN_SENS1 Gpio::F3
// A2 AIN2 no code to use ADC3 as analog
#define H144_IN_SENS2 Gpio::F4
// A3 AIN3 no code to use ADC3 as analog
#define H144_IN_SENS3 Gpio::F5
// A4 AIN4 no code to use ADC3 as analog
#define H144_IN_SENS4 Gpio::F6

// IN_O2S AIN13 A13 PA0
#define H144_IN_O2S EFI_ADC_0
// IN_O2S2 AIN12 PA1
#define H144_IN_O2S2 EFI_ADC_1

// IN_D1(old) or IN_DIG8(new/mega-module)
#define H144_IN_D_1 Gpio::E12
// IN_D2(old) or IN_DIG9(new/mm)
#define H144_IN_D_2 Gpio::E13
// IN_D3(old) or IN_DIG10(new)
#define H144_IN_D_3 Gpio::E14
// IN_D4(old) or IN_DIG11(new)
#define H144_IN_D_4 Gpio::E15

// A22
#define H144_IN_D_AUX3 Gpio::C5
// A20 AIN20
#define H144_IN_D_AUX4 Gpio::A7

#define H176_LS_1 Gpio::G7
#define H176_LS_2 Gpio::G8
#define H176_LS_3 Gpio::D11
#define H176_LS_4 Gpio::D10
#define H176_LS_5 Gpio::D9
#define H176_LS_6 Gpio::F12
#define H176_LS_7 Gpio::F13
#define H176_LS_8 Gpio::F14

#define H176_OUT_PWM1 Gpio::D13
#define H176_OUT_PWM2 Gpio::C6
#define H176_OUT_PWM3 Gpio::C7

#define H176_OUT_IO6 Gpio::H15
#define H176_OUT_IO9 Gpio::I1
#define H176_OUT_IO10 Gpio::I0

// same pins 144 and 176
#define H176_CAN_RX Gpio::D0
#define H176_CAN_TX Gpio::D1

#define H176_IN_A16 Gpio::F10

#define H_SPI1_MOSI Gpio::B5
#define H_SPI1_MISO Gpio::B4
#define H_SPI1_SCK Gpio::B3
#define H_SPI1_CS1 Gpio::B6
#define H_SPI1_CS2 Gpio::B7

// looks like same pins 144 and 176
#define H_SPI2_MOSI Gpio::B15
#define H_SPI2_MISO Gpio::B14
#define H_SPI2_SCK Gpio::B13
#define H_SPI2_CS Gpio::B12

#define H_SPI3_MOSI Gpio::C12
#define H_SPI3_MISO Gpio::C11
#define H_SPI3_SCK Gpio::C10
#define H_SPI3_CS Gpio::A15

inline void setHellenSdCardSpi1NoCS() {
	engineConfiguration->isSdCardEnabled = true;
	engineConfiguration->sdCardSpiDevice = SPI_DEVICE_1;
	engineConfiguration->spi1mosiPin = H_SPI1_MOSI;
	engineConfiguration->spi1misoPin = H_SPI1_MISO;
	engineConfiguration->spi1sckPin = H_SPI1_SCK;
	engineConfiguration->is_enabled_spi_1 = true;
}

inline void setHellenSdCardSpi1() {
	setHellenSdCardSpi1NoCS();
	engineConfiguration->sdCardCsPin = H_SPI1_CS1;
}

inline void setHellen64SdCardSpi1() {
	setHellenSdCardSpi1NoCS();
	engineConfiguration->sdCardCsPin = H64_SPI1_CS1;
}

inline void setHellenSdCardSpi2() {
	engineConfiguration->isSdCardEnabled = true;
	engineConfiguration->sdCardSpiDevice = SPI_DEVICE_2;
	engineConfiguration->spi2mosiPin = H_SPI2_MOSI;
	engineConfiguration->spi2misoPin = H_SPI2_MISO;
	engineConfiguration->spi2sckPin = H_SPI2_SCK;
	engineConfiguration->sdCardCsPin = H_SPI2_CS;
	engineConfiguration->is_enabled_spi_2 = true;
}

inline void setHellenSdCardSpi3() {
	engineConfiguration->isSdCardEnabled = true;
	engineConfiguration->sdCardSpiDevice = SPI_DEVICE_3;
	engineConfiguration->spi3mosiPin = H_SPI3_MOSI;
	engineConfiguration->spi3misoPin = H_SPI3_MISO;
	engineConfiguration->spi3sckPin = H_SPI3_SCK;
	engineConfiguration->sdCardCsPin = H_SPI3_CS;
	engineConfiguration->is_enabled_spi_3 = true;
}

// *pullups* files CLT R211 IAT R213
#define HELLEN_DEFAULT_AT_PULLUP 4700

inline void setDefaultHellenAtPullUps(){
	engineConfiguration->clt.config.bias_resistor = HELLEN_DEFAULT_AT_PULLUP;
	engineConfiguration->iat.config.bias_resistor = HELLEN_DEFAULT_AT_PULLUP;
}

inline void setHellenMMbaro() {
	engineConfiguration->lps25BaroSensorScl = Gpio::B10;
	engineConfiguration->lps25BaroSensorSda = Gpio::B11;
}
