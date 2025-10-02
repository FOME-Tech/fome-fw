//Aug 18, 2025 @Creesic

#include "pch.h"
#include "trigger_structure.h"
#include "trigger_bmw.h"

void initializeVvtN63TU(TriggerWaveform *s) {
    s->initialize(FOUR_STROKE_CAM_SENSOR, SyncEdge::Fall);
    
    s->shapeWithoutTdc = true;

	s->setTriggerSynchronizationGap2(1.4, 1.6);

    s->addEvent360(65, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(150, false, TriggerWheel::T_PRIMARY);

    s->addEvent360(180, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(240, false, TriggerWheel::T_PRIMARY);
    
    s->addEvent360(335, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(360, false, TriggerWheel::T_PRIMARY);
}
