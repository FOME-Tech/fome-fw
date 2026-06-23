package com.rusefi;

import com.rusefi.core.Pair;
import com.rusefi.output.ConfigStructure;

public interface ConfigField {
    ConfigField VOID = new ConfigField() {
        @Override
        public boolean isArray() {
            return false;
        }

        @Override
        public boolean isBit() {
            return false;
        }

        @Override
        public boolean isDirective() {
            return false;
        }

        @Override
        public String getComment() {
            return null;
        }

        @Override
        public String getName() {
            return null;
        }

        @Override
        public String getType() {
            return null;
        }

        @Override
        public ReaderState getState() {
            return null;
        }

        @Override
        public boolean isFromIterate() {
            return false;
        }
    };

    boolean isArray();

    boolean isBit();

    boolean isDirective();

    String getComment();

    String getName();

    String getType();

    ReaderState getState();

    boolean isFromIterate();
}
