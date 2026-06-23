package com.rusefi.util;

import java.io.*;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;

/**
 * Buffers content in memory and only writes to disk if the new content differs from what is already
 * on disk. This keeps file modification times stable across regenerations when nothing actually
 * changed, so incremental builds only rebuild the translation units whose generated inputs changed.
 */
public class LazyFile implements Output {
    public static final String TEST = "test_file_name";

    private final String filename;
    private final boolean isTest;
    private final StringBuilder content = new StringBuilder();

    public LazyFile(String filename) {
        this.filename = filename;
        this.isTest = TEST.equals(filename);
    }

    @Override
    public void write(String line) {
        content.append(line);
    }

    @Override
    public void close() throws IOException {
        if (isTest)
            return;
        writeIfChanged(filename, content.toString().getBytes(IoUtils.CHARSET));
    }

    /**
     * Writes the given content to the file only if it differs from the current on-disk content,
     * leaving the file (and its mtime) untouched otherwise.
     */
    public static void writeIfChanged(String filename, byte[] newContent) throws IOException {
        Path path = Paths.get(filename);
        if (Files.exists(path)) {
            byte[] existing = Files.readAllBytes(path);
            if (Arrays.equals(existing, newContent)) {
                return;
            }
        } else {
            Path parent = path.getParent();
            if (parent != null) {
                Files.createDirectories(parent);
            }
        }
        Files.write(path, newContent);
    }
}
