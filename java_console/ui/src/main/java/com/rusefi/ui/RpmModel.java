package com.rusefi.ui;

import com.rusefi.core.Sensor;
import com.rusefi.core.SensorCentral;

import java.util.ArrayList;
import java.util.List;

/**
 * Model for RPM reading with a feature of smoothing the displayed value: new value is not displayed if updated
 * value is within 5% range around currently displayed value. Here we rely on the fact that RPM values are coming in
 * constantly
 * <p/>
 * Date: 12/27/12
 * Andrey Belomutskiy, (c) 2013-2020
 */
public class RpmModel {
    private static final RpmModel INSTANCE = new RpmModel();
    private int value;
    private final List<RpmListener> listeners = new ArrayList<>();

    public static RpmModel getInstance() {
        return INSTANCE;
    }

    private RpmModel() {
        SensorCentral.getInstance().addListener(Sensor.RPMValue, value -> setValue((int) value));
    }

    public void setValue(int rpm) {
        value = rpm;

        for (RpmListener listener : listeners)
            listener.onRpmChange(this);
    }

    public int getValue() {
        return value;
    }

    public void addListener(RpmListener listener) {
        listeners.add(listener);
    }

    interface RpmListener {
        void onRpmChange(RpmModel rpm);
    }
}
