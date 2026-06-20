#pragma once

#include "can.h"

void initCanImu();
void processCanRxImu(const CANRxFrame& frame);
