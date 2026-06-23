package com.rusefi.output;

import com.rusefi.ConfigField;

import java.util.List;

public interface ConfigStructure {
    String UNUSED_ANYTHING_PREFIX = "unused";

    List<ConfigField> getTsFields();
}
