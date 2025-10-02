//Aug 18, 2025 @Creesic

#include "pch.h"
#include "trigger_structure.h"
#include "trigger_bmw.h"

void initializeVvtN63TU(TriggerWaveform *s) {
    s->initialize(FOUR_STROKE_CAM_SENSOR, SyncEdge::Fall);
    
    s->shapeWithoutTdc = true;

	s->setTriggerSynchronizationGap(0.5);
	s->setSecondTriggerSynchronizationGap(1.583);
	s->setThirdTriggerSynchronizationGap(1.083);

    s->addEvent360(85, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(115, false, TriggerWheel::T_PRIMARY);

    s->addEvent360(175, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(270, false, TriggerWheel::T_PRIMARY);
    
    s->addEvent360(295, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(360, false, TriggerWheel::T_PRIMARY);
}
