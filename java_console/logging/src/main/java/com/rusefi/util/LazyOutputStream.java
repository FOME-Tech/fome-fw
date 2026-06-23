package com.rusefi.util;

import java.io.ByteArrayOutputStream;
import java.io.IOException;

/**
 * An in-memory output stream that, on close, writes its contents to the target file only if they
 * differ from what is already on disk (see {@link LazyFile#writeIfChanged}). Use this in place of a
 * direct file stream so that regenerating identical content does not bump the file's mtime and
 * trigger needless recompilation.
 */
public class LazyOutputStream extends ByteArrayOutputStream {
    private final String filename;

    public LazyOutputStream(String filename) {
        this.filename = filename;
    }

    @Override
    public void close() throws IOException {
        super.close();
        LazyFile.writeIfChanged(filename, toByteArray());
    }
}
