package com.rusefi.output;

import com.rusefi.*;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import static com.rusefi.ConfigFieldImpl.BOOLEAN_T;

/**
 * Mutable representation of a list of related {@link ConfigFieldImpl}
 * <p>
 * Andrey Belomutskiy, (c) 2013-2020
 * 1/15/15
 */
public class ConfigStructureImpl implements ConfigStructure {
    public static final String ALIGNMENT_FILL_AT = "alignmentFill_at_";

    private final List<ConfigField> tsFields = new ArrayList<>();

    @Override
    public List<ConfigField> getTsFields() {
        return tsFields;
    }
}
