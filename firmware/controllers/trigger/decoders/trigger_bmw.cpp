//Aug 18, 2025 @Creesic

#include "pch.h"
#include "trigger_structure.h"
#include "trigger_bmw.h"

void initializeVvtN63TU(TriggerWaveform *s) {
    s->initialize(FOUR_STROKE_CAM_SENSOR, SyncEdge::Both);
    
    s->shapeWithoutTdc = true;

	s->setTriggerSynchronizationGap(0.581);
	s->setSecondTriggerSynchronizationGap(1.722);

    s->addEvent360(25, TriggerValue::RISE); //pos1
    s->addEvent360(90, TriggerValue::FALL); //pos2
    
    s->addEvent360(180, TriggerValue::RISE); //pos3
    s->addEvent360(200, TriggerValue::FALL); //pos4
    
    s->addEvent360(270, TriggerValue::RISE); //pos5
    s->addEvent360(360, TriggerValue::FALL); //pos6
}
