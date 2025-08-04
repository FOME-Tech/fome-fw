package com.rusefi.output;

import com.rusefi.*;
import com.rusefi.newparse.DefinitionsState;
import com.rusefi.newparse.parsing.Definition;
import com.rusefi.util.SystemOut;

import java.io.IOException;

public class SignatureConsumer {
    public static void storeUniqueBuildId(ReaderStateImpl state, DefinitionsState parseState, String tsPath, FirmwareVersion uniqueId) {
        // store a hash as a built-in variable

        // nasty trick - do not insert signature into live data files
        if (tsPath != null) {
            parseState.addDefinition(state.getVariableRegistry(),
                    ConfigDefinition.SIGNATURE_HASH, uniqueId.encode(), Definition.OverwritePolicy.NotAllowed);
        }
    }
}
