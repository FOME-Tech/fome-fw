//Aug 18, 2025 @Creesic

#include "pch.h"
#include "trigger_structure.h"
#include "trigger_bmw.h"

void initializeVvtN63TU(TriggerWaveform *s) {
    s->initialize(FOUR_STROKE_CAM_SENSOR, SyncEdge::Fall);

	setTriggerSynchronizationGap3(0, 0.9, 1.8);
	setTriggerSynchronizationGap3(1, 0.9, 1.8);
	setTriggerSynchronizationGap3(2, 0.3, 0.8);

    s->addEvent360(85, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(115, false, TriggerWheel::T_PRIMARY);

    s->addEvent360(175, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(270, false, TriggerWheel::T_PRIMARY);
    
    s->addEvent360(295, true, TriggerWheel::T_PRIMARY);
    s->addEvent360(360, false, TriggerWheel::T_PRIMARY);
}
