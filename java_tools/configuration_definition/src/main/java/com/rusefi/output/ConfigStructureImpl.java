package com.rusefi.output;

import com.rusefi.*;

import java.util.ArrayList;
import java.util.List;

/**
 * Mutable representation of a list of related {@link ConfigFieldImpl}
 * <p>
 * Andrey Belomutskiy, (c) 2013-2020
 * 1/15/15
 */
public class ConfigStructureImpl implements ConfigStructure {
    private final List<ConfigField> tsFields = new ArrayList<>();

    @Override
    public List<ConfigField> getTsFields() {
        return tsFields;
    }
}
