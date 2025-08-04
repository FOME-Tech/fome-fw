package com.rusefi.binaryprotocol;

import com.opensr5.ConfigurationImage;
import com.rusefi.config.generated.Fields;

public class BinaryProtocolState {
    private final Object imageLock = new Object();
    private ConfigurationImage controller;
    /**
     * Snapshot of current gauges status
     * @see Fields#TS_OUTPUT_COMMAND
     */
    public void setController(ConfigurationImage controller) {
        synchronized (imageLock) {
            this.controller = controller.clone();
        }
    }

    public ConfigurationImage getControllerConfiguration() {
        synchronized (imageLock) {
            if (controller == null)
                return null;
            return controller.clone();
        }
    }
}
