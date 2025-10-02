//Aug 18, 2025 @Creesic

#include "pch.h"
#include "trigger_structure.h"
#include "trigger_bmw.h"

void initializeVvtN63TU(TriggerWaveform *s) {
    s->initialize(FOUR_STROKE_CAM_SENSOR, SyncEdge::Fall);
    
    s->shapeWithoutTdc = true;

	s->setTriggerSynchronizationGap2(0.56f, 0.70f);
	s->setSecondTriggerSynchronizationGap2(1.28f, 1.62f);

    s->addEvent360(25, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(90, false, TriggerWheel::T_PRIMARY);

    s->addEvent360(180, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(200, false, TriggerWheel::T_PRIMARY);
    
    s->addEvent360(270, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(360, false, TriggerWheel::T_PRIMARY);
}
