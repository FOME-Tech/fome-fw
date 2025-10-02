//Aug 18, 2025 @Creesic

#include "pch.h"
#include "trigger_structure.h"
#include "trigger_bmw.h"

void initializeVvtN63TU(TriggerWaveform *s) {
    s->initialize(FOUR_STROKE_CAM_SENSOR, SyncEdge::Fall);
    
    s->shapeWithoutTdc = true;

	s->setTriggerSynchronizationGap2(1.4, 1.6);

    s->addEvent360(0, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(85, false, TriggerWheel::T_PRIMARY);

    s->addEvent360(115, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(175, false, TriggerWheel::T_PRIMARY);
    
    s->addEvent360(270, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(295, false, TriggerWheel::T_PRIMARY);
}
