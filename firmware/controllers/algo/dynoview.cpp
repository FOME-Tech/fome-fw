/*
 * @file dynoview.cpp
 *
 * @date Nov 29, 2020
 * @author Alexandru Miculescu, (c) 2012-2020
 */

#include "pch.h"

#if EFI_DYNO_VIEW
#include "dynoview.h"

static DynoView dynoInstance;

void DynoView::update(vssSrc src) {

    efitimeus_t timeNow, deltaTime = 0.0;
    float speed,deltaSpeed = 0.0;
    timeNow = getTimeNowUs();
    speed = Sensor::getOrZero(SensorType::VehicleSpeed);
    if (src == ICU) {
        speed = efiRound(speed,1.0);
    } else {
        //use speed with 0.001 precision from source CAN
        speed = efiRound(speed,0.001);
    }

    if(timeStamp != 0) {

        if (vss != speed) {
            deltaTime = timeNow - timeStamp;
            if (vss > speed) {
                deltaSpeed = (vss - speed);
                direction = 1; //decceleration
            } else {
                deltaSpeed = speed - vss;
                direction = 0; //acceleration
            }

            //save data
            timeStamp = timeNow;
            vss = speed;
        }
        
        //updating here would display acceleration = 0 at constant speed
        updateAcceleration(deltaTime, deltaSpeed);

        updateHP();

    } else {
        //ensure we grab init values
        timeStamp = timeNow;
        vss = speed;
    }
}

/**
 * input units: deltaSpeed in km/h
 *              deltaTime in uS
 */
void DynoView::updateAcceleration(efitimeus_t deltaTime, float deltaSpeed) {
    if (deltaSpeed != 0.0) {
        acceleration = ((deltaSpeed / 3.6) / (deltaTime / US_PER_SECOND_F));
        if (direction) {
            //decceleration
            acceleration *= -1;
        }
    } else {
        acceleration = 0.0;
    }
}

/**
 * E = m*a
 * ex. 900 (kg) * 1.5 (m/s^2) = 1350N
 * P = F*V
 * 1350N * 35(m/s) = 47250Watt (35 m/s is the final velocity)
 * 47250 * (1HP/746W) = 63HP
 * https://www.youtube.com/watch?v=FnN2asvFmIs
 * we do not take resistence into account right now.
 */
void DynoView::updateHP() {

    //these are actually at the wheel
    //we would need final drive to calcualte the correct torque at the wheel
    if (acceleration != 0) {
        engineForce = engineConfiguration->vehicleWeight * acceleration;
        enginePower = engineForce * (vss / 3.6);
        engineHP = enginePower / 746;
        if (Sensor::getOrZero(SensorType::Rpm) > 0) {
            engineTorque = ((engineHP * 5252) / Sensor::getOrZero(SensorType::Rpm));
        }
    } else {
        //we should calculate static power
    }

}

#if EFI_UNIT_TEST
void DynoView::setAcceleration(float a) {
    acceleration = a;
}
#endif

float DynoView::getAcceleration() {
    return acceleration;
}

int DynoView::getEngineForce() {
    return engineForce;
}

int DynoView::getEnginePower() {
    return (enginePower/1000);
}

int DynoView::getEngineHP() {
    return engineHP;
}

int DynoView::getEngineTorque() {
    return (engineTorque/0.73756);
}


float getDynoviewAcceleration() {
    return dynoInstance.getAcceleration();
}

int getDynoviewPower() {
    return dynoInstance.getEnginePower();
}

/**
 * Periodic update function called from SlowCallback.
 * Only updates if we have Vss from input pin.
 */
void updateDynoView() {
	if (isBrainPinValid(engineConfiguration->vehicleSpeedSensorInputPin) &&
		(!engineConfiguration->enableCanVss)) {
		dynoInstance.update(ICU);
	}
}

/**
 * This function is called after every CAN msg received, we process it
 * as soon as we can to be more acurate.
 */ 
void updateDynoViewCan() {
    if (!engineConfiguration->enableCanVss) {
        return;
    }
    
    dynoInstance.update(CAN);
}

#endif /* EFI_DYNO_VIEW */
