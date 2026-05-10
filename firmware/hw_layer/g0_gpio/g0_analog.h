#pragma once

#include "pch.h"
#include "adc_provider.h"

void startG070SpiAdcProvider();
bool readG070DigitalInput(size_t idx);
