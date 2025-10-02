//Aug 18, 2025 @Creesic

#include "pch.h"
#include "trigger_structure.h"
#include "trigger_bmw.h"

void initializeVvtN63TU(TriggerWaveform *s) {
    s->initialize(FOUR_STROKE_CAM_SENSOR, SyncEdge::Fall);
    
    s->shapeWithoutTdc = true;

	s->setTriggerSynchronizationGap2(1.4, 1.6);

s->addEvent720(13,  true,  TriggerWheel::T_PRIMARY);
s->addEvent720(185,  false, TriggerWheel::T_PRIMARY);
	
s->addEvent720(246,  true,  TriggerWheel::T_PRIMARY);
s->addEvent720(365,  false, TriggerWheel::T_PRIMARY);
	
s->addEvent720(551,  true,  TriggerWheel::T_PRIMARY);
s->addEvent720(604,  false, TriggerWheel::T_PRIMARY);
}
