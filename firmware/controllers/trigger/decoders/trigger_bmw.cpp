//Aug 18, 2025 @Creesic

#include "pch.h"
#include "trigger_structure.h"
#include "trigger_bmw.h"

void initializeVvtN63TU(TriggerWaveform *s) {
    s->initialize(FOUR_STROKE_CAM_SENSOR, SyncEdge::Fall);
    
    s->shapeWithoutTdc = true;

	s->setTriggerSynchronizationGap(0.581);
	s->setSecondTriggerSynchronizationGap(1.722);

    s->addEvent360(25, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(90, false, TriggerWheel::T_PRIMARY);

    s->addEvent360(180, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(200, false, TriggerWheel::T_PRIMARY);
    
    s->addEvent360(270, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(360, false, TriggerWheel::T_PRIMARY);
}
