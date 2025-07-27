package com.rusefi.core;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.net.URL;

import static com.rusefi.core.FileUtil.RUSEFI_SETTINGS_FOLDER;

public class SignatureHelper {
    // todo: find a way to reference Fields.PROTOCOL_SIGNATURE_PREFIX
    private static final String PREFIX = "rusEFI ";

    public static RusEfiSignature parse(String signature) {
        if (signature == null || !signature.startsWith(PREFIX))
            return null;
        signature = signature.substring(PREFIX.length()).trim();
        String[] elements = signature.split("\\.");
        if (elements.length != 6)
            return null;

        String branch = elements[0];
        String year = elements[1];
        String month = elements[2];
        String day = elements[3];
        String bundleTarget = elements[4];
        String hash = elements[5];

        return new RusEfiSignature(branch, year, month, day, bundleTarget, hash);
    }
}
