package com.rusefi.f4discovery;

import com.rusefi.RusefiTestBase;
import com.rusefi.enums.engine_type_e;
import com.rusefi.functional_tests.EcuTestHelper;
import org.junit.Test;

import static com.rusefi.config.generated.Fields.CMD_ENGINESNIFFERRPMTHRESHOLD;
import static com.rusefi.functional_tests.EcuTestHelper.FAIL;

public class HighRevTest extends RusefiTestBase {
    @Test
    public void testVW() {
        ecu.setEngineType(engine_type_e.VW_ABA);
        // trying to disable engine sniffer to help https://github.com/rusefi/rusefi/issues/1849
        ecu.sendCommand("set " + CMD_ENGINESNIFFERRPMTHRESHOLD + " 100");
        ecu.changeRpm(900);
        // first let's get to expected RPM
        EcuTestHelper.assertRpmDoesNotJump(6000, 5, 40, FAIL, ecu.commandQueue);
    }

    @Test
    public void testV12() {
        ecu.setEngineType(engine_type_e.FRANKENSO_BMW_M73_F);
        ecu.changeRpm(700);
        // first let's get to expected RPM
        EcuTestHelper.assertRpmDoesNotJump(6000, 5, 40, FAIL, ecu.commandQueue);

        // tests bug 1873
        EcuTestHelper.assertRpmDoesNotJump(60, 5, 110, FAIL, ecu.commandQueue);
    }
    @Test
    public void testBenelliR3() {
        ecu.setEngineType(engine_type_e.FRANKENSO_BMW_M73_F);
        // set idle RPM
        ecu.changeRpm(1500);
        // first let's get to RPM near limit
        EcuTestHelper.assertRpmDoesNotJump(11000, 5, 40, FAIL, ecu.commandQueue);

        // tests RpmLimit
        EcuTestHelper.assertRpmDoesNotJump(12000, 5, 110, FAIL, ecu.commandQueue);
    }
}
