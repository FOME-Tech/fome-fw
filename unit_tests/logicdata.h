/*
 * @file logicdata.h
 *
 *  Created on: Jul 19, 2020
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#include <vector>
#include <cstdint>

struct CompositeEvent {
    uint32_t timestamp;
    bool primaryTrigger;
    bool secondaryTrigger;
    bool isTDC;
    bool sync;
    bool coil;
    bool injector;
};

void writeFile(const char * fileName, const std::vector<CompositeEvent>& events);
