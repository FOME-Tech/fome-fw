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
        public int getSize(ConfigField next) {
            return 0;
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
        public int getElementSize() {
            return 0;
        }

        @Override
        public boolean isIterate() {
            return false;
        }

        @Override
        public ReaderState getState() {
            return null;
        }

        @Override
        public Pair<Integer, Integer> autoscaleSpecPair() {
            return null;
        }

        @Override
        public double getMin() {
            return 0;
        }

        @Override
        public double getMax() {
            return 0;
        }

        @Override
        public int getDigits() {
            return 0;
        }

        @Override
        public boolean isFromIterate() {
            return false;
        }
    };

    boolean isArray();

    boolean isBit();

    boolean isDirective();

    int getSize(ConfigField next);

    String getComment();

    String getName();

    String getType();

    int getElementSize();

    boolean isIterate();

    ReaderState getState();

    Pair<Integer, Integer> autoscaleSpecPair();

    double getMin();

    double getMax();

    int getDigits();

    boolean isFromIterate();
}
