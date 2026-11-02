package com.rusefi.pinout;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.Reader;
import java.io.Writer;
import java.util.List;

public interface BoardInputs {
    List<?> getBoardYamlKeys();

    Reader getReader(Object yamlKey) throws FileNotFoundException;

    /**
     * Name of a yaml source as written into generated headers, so it must not depend on who ran the generator.
     */
    default String getDisplayName(Object yamlKey) {
        return yamlKey.toString();
    }

    String getName();

    List<String> getInputFiles();

    Writer getWriter() throws IOException;
}
