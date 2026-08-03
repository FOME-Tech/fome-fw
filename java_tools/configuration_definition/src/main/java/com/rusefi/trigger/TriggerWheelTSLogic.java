package com.rusefi.trigger;

import com.rusefi.VariableRegistry;
import com.rusefi.newparse.DefinitionsState;
import com.rusefi.newparse.parsing.Definition;

public class TriggerWheelTSLogic {

    private static final String TRIGGER_TYPE_WITHOUT_KNOWN_LOCATION = "TRIGGER_TYPE_WITHOUT_KNOWN_LOCATION";
    private static final String TRIGGER_TYPE_WITH_SECOND_WHEEL = "TRIGGER_TYPE_WITH_SECOND_WHEEL";
    private static final String TRIGGER_CRANK_BASED = "TRIGGER_CRANK_BASED";

    public void execute(String folder, VariableRegistry variableRegistry, DefinitionsState definitionsState) {
        if (folder == null) {
            System.out.println(getClass() + ": Folder not specified");
            return;
        }
        StringBuilder triggerTypesWithoutKnownLocation = new StringBuilder();
        StringBuilder triggerTypesWithSecondWheel = new StringBuilder();
        StringBuilder triggerTypesCrankBased = new StringBuilder();


        TriggerWheelInfo.readWheels(folder, wheelInfo -> {
            // System.out.println("onWheel " + wheelInfo.getTriggerName());

            if (!wheelInfo.isHardcodedOperationMode()) {
                appendOrIfNotEmpty(triggerTypesWithoutKnownLocation);
                triggerTypesWithoutKnownLocation.append("trigger_type == ").append(wheelInfo.getId());
            }

            if (wheelInfo.isHasSecondChannel()) {
                appendOrIfNotEmpty(triggerTypesWithSecondWheel);
                triggerTypesWithSecondWheel.append("trigger_type == ").append(wheelInfo.getId());
            }

            if (wheelInfo.isCrankBased()) {
                appendOrIfNotEmpty(triggerTypesCrankBased);
                triggerTypesCrankBased.append("trigger_type == ").append(wheelInfo.getId());
            }

        });

        /*
         * these are templated into tunerstudio.template.ini file
         * note that TT_TOOTHED_WHEEL is not mentioned in the meta file, we handle it manually right in tunerstudio.template.ini file
         *
         * Register via DefinitionsState (not just VariableRegistry): the newparse TsWriter resolves
         * @@...@@ tokens through ParseState.findDefinition(), so registering only into VariableRegistry
         * leaves these as "MISSING DEFINITION" in the generated ini.
         */
        definitionsState.addDefinition(variableRegistry, TRIGGER_TYPE_WITHOUT_KNOWN_LOCATION,
                triggerTypesWithoutKnownLocation.toString(), Definition.OverwritePolicy.NotAllowed);
        definitionsState.addDefinition(variableRegistry, TRIGGER_TYPE_WITH_SECOND_WHEEL,
                triggerTypesWithSecondWheel.toString(), Definition.OverwritePolicy.NotAllowed);
        definitionsState.addDefinition(variableRegistry, TRIGGER_CRANK_BASED,
                triggerTypesCrankBased.toString(), Definition.OverwritePolicy.NotAllowed);
    }

    private void appendOrIfNotEmpty(StringBuilder triggerTypesWithSecondWheel) {
        if (!triggerTypesWithSecondWheel.isEmpty())
            triggerTypesWithSecondWheel.append(" || ");
    }
}
