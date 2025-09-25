//Aug 18, 2025 @Creesic

#include "pch.h"
#include "trigger_structure.h"
#include "trigger_bmw.h"

void initializeVvtN63TU(TriggerWaveform *s) {
    s->initialize(FOUR_STROKE_CAM_SENSOR, SyncEdge::Both);
    // True means it's a cam wheel
    s->isSecondWheelCam = true;

    // per-state ratio acceptance windows
    s->setTriggerSynchronizationGap3(0, 1.8f, 3.2f);   // around 2.60
    s->setTriggerSynchronizationGap3(1, 1.10f, 1.80f); // around 1.385
    s->setTriggerSynchronizationGap3(2, 0.15f, 0.35f); // around 0.222
    s->setTriggerSynchronizationGap3(3, 2.5f, 4.5f);   // around 3.50
    s->setTriggerSynchronizationGap3(4, 1.05f, 1.65f); // around 1.286
    s->setTriggerSynchronizationGap3(5, 0.20f, 0.50f); // around 0.278

    //r0 = g0/g5 = 65/25 = 2.600000  Gap ratio calculation
    //r1 = g1/g0 = 90/65 = 1.384615
    //r2 = g2/g1 = 20/90 = 0.222222
    //r3 = g3/g2 = 70/20 = 3.500000
    //r4 = g4/g3 = 90/70 = 1.285714
    //r5 = g5/g4 = 25/90 = 0.277778

    // Edges: (angle, isRising, wheel)
    s->addEvent720( 50.0f,  true,  TriggerWheel::T_PRIMARY);
    s->addEvent720(180.0f,  false, TriggerWheel::T_PRIMARY);
    s->addEvent720(360.0f,  true,  TriggerWheel::T_PRIMARY);
    s->addEvent720(400.0f,  false, TriggerWheel::T_PRIMARY);
    s->addEvent720(540.0f,  true,  TriggerWheel::T_PRIMARY);
    s->addEvent720(720.0f,  false, TriggerWheel::T_PRIMARY);
}
