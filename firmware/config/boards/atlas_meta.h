#pragma once

#if !HW_ATLAS
#error "this file intended for only Atlas hardware"
#endif

#define PROTEUS_LS_1 Gpio::G5
#define PROTEUS_LS_2 Gpio::G6
#define PROTEUS_LS_3 Gpio::G7
#define PROTEUS_LS_4 Gpio::G8
#define PROTEUS_LS_5 Gpio::C6
#define PROTEUS_LS_6 Gpio::C7
#define PROTEUS_LS_7 Gpio::A15
#define PROTEUS_LS_8 Gpio::D3
#define PROTEUS_LS_9 Gpio::B4
#define PROTEUS_LS_10 Gpio::B5
#define PROTEUS_LS_11 Gpio::B6
#define PROTEUS_LS_12 Gpio::B7
#define PROTEUS_LS_13 Gpio::B8
#define PROTEUS_LS_14 Gpio::B9
#define PROTEUS_LS_15 Gpio::E0
#define PROTEUS_LS_16 Gpio::E1

#define PROTEUS_HS_1 Gpio::G4
#define PROTEUS_HS_2 Gpio::G3
#define PROTEUS_HS_3 Gpio::G2
#define PROTEUS_HS_4 Gpio::D15

#define PROTEUS_IGN_1 Gpio::B3
#define PROTEUS_IGN_2 Gpio::G15
#define PROTEUS_IGN_3 Gpio::G14
#define PROTEUS_IGN_4 Gpio::G13
#define PROTEUS_IGN_5 Gpio::G12
#define PROTEUS_IGN_6 Gpio::G11
#define PROTEUS_IGN_7 Gpio::G10
#define PROTEUS_IGN_8 Gpio::G9
#define PROTEUS_IGN_9 Gpio::D7
#define PROTEUS_IGN_10 Gpio::D6
#define PROTEUS_IGN_11 Gpio::D5
#define PROTEUS_IGN_12 Gpio::D4

#define PROTEUS_IN_ANALOG_VOLT_1 EFI_ADC_0
#define PROTEUS_IN_ANALOG_VOLT_2 EFI_ADC_1
#define PROTEUS_IN_ANALOG_VOLT_3 EFI_ADC_2
#define PROTEUS_IN_ANALOG_VOLT_4 EFI_ADC_3
#define PROTEUS_IN_ANALOG_VOLT_5 EFI_ADC_4
#define PROTEUS_IN_ANALOG_VOLT_6 EFI_ADC_5
#define PROTEUS_IN_ANALOG_VOLT_7 EFI_ADC_6
#define PROTEUS_IN_ANALOG_VOLT_8 EFI_ADC_7
#define PROTEUS_IN_ANALOG_VOLT_9 EFI_ADC_8
#define PROTEUS_IN_ANALOG_VOLT_10 EFI_ADC_9
#define PROTEUS_IN_ANALOG_VOLT_11 EFI_ADC_10

#define PROTEUS_IN_ANALOG_TEMP_1 EFI_ADC_11
#define PROTEUS_IN_ANALOG_TEMP_2 EFI_ADC_12
#define PROTEUS_IN_ANALOG_TEMP_3 EFI_ADC_13
#define PROTEUS_IN_ANALOG_TEMP_4 EFI_ADC_14

#define PROTEUS_VR_1 Gpio::E14
#define PROTEUS_VR_2 Gpio::E15

#define PROTEUS_DIGITAL_1 Gpio::G0
#define PROTEUS_DIGITAL_2 Gpio::G1
#define PROTEUS_DIGITAL_3 Gpio::E10
#define PROTEUS_DIGITAL_4 Gpio::E11
#define PROTEUS_DIGITAL_5 Gpio::E12
#define PROTEUS_DIGITAL_6 Gpio::E13
