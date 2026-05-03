package com.rusefi;

import com.rusefi.binaryprotocol.BinaryProtocol;
import com.rusefi.config.generated.Fields;
import com.rusefi.core.preferences.storage.PersistentConfiguration;
import com.rusefi.ui.MessagesView;
import com.rusefi.ui.UIContext;
import com.rusefi.ui.util.UiUtils;
import org.jetbrains.annotations.NotNull;

import javax.swing.*;
import java.awt.*;
import java.awt.event.ActionListener;

import static com.rusefi.CommandControl.TEST;
import static com.rusefi.config.generated.Fields.*;

public class BenchTestPane {
    private static final String CMD_FORCE_G0_UPDATE = "force_g0_update";
    private static final int G0_UPDATE_TIMEOUT_MS = 120_000;

    private final JPanel content = new JPanel(new GridLayout(2, 5));
    private final UIContext uiContext;

    public BenchTestPane(UIContext uiContext, PersistentConfiguration config) {
        this.uiContext = uiContext;
        content.setBorder(BorderFactory.createEmptyBorder(20, 20, 20, 20));

        content.add(grabPerformanceTrace());
        content.add(createFanTest());
        content.add(createAcRelayTest());
        content.add(createFuelPumpTest());
        content.add(createSparkTest());
        content.add(createInjectorTest());
        content.add(createMILTest());
        content.add(createIdleTest());
        content.add(createStarterTest());
        content.add(new CommandControl(uiContext, "Reboot", "", "Reboot") {
            @Override
            protected String getCommand() {
                return Fields.CMD_REBOOT;
            }
        }.getContent());
        content.add(new CommandControl(uiContext,"Reboot to DFU", "", "Reboot to DFU") {
            @Override
            protected String getCommand() {
                return Fields.CMD_REBOOT_DFU;
            }
        }.getContent());
        content.add(createForceG0Update());
        content.add(new MessagesView(config.getRoot()).messagesScroll);
    }

    private Component createForceG0Update() {
        CommandControl panel = new FixedCommandControl(uiContext, "G0 Firmware", "", "Force Update", CMD_FORCE_G0_UPDATE) {
            @NotNull
            @Override
            protected ActionListener createButtonListener() {
                return e -> {
                    int choice = JOptionPane.showConfirmDialog(
                            content,
                            "Force G0 firmware update?",
                            "G0 Firmware Update",
                            JOptionPane.OK_CANCEL_OPTION,
                            JOptionPane.WARNING_MESSAGE);

                    if (choice == JOptionPane.OK_OPTION) {
                        uiContext.getCommandQueue().write(CMD_FORCE_G0_UPDATE, G0_UPDATE_TIMEOUT_MS);
                    }
                };
            }
        };
        return panel.getContent();
    }

    private Component grabPerformanceTrace() {
        JButton button = new JButton("Grab PTrace");
        ActionListener actionListener = e -> uiContext.getLinkManager().COMMUNICATION_EXECUTOR.execute(() -> {
            BinaryProtocol bp = uiContext.getLinkManager().getCurrentStreamState();
            PerformanceTraceHelper.grabPerformanceTrace(button, bp);
        });
        button.addActionListener(actionListener);
        return UiUtils.wrap(button);
    }

    private Component createMILTest() {
        CommandControl panel = new CommandControl(uiContext,"MIL", "check_engine.jpg", TEST) {
            @NotNull
            protected String getCommand() {
                return Fields.CMD_MIL_BENCH;
            }
        };
        return panel.getContent();
    }

    private Component createIdleTest() {
        CommandControl panel = new CommandControl(uiContext,"Idle Valve", "idle_valve.png", TEST) {
            @NotNull
            protected String getCommand() {
                return "idlebench";
            }
        };
        return panel.getContent();
    }

    private Component createStarterTest() {
        CommandControl panel = new FixedCommandControl(uiContext, "Starter", "", TEST, CMD_STARTER_BENCH);
        return panel.getContent();
    }

    private Component createFanTest() {
        CommandControl panel = new FixedCommandControl(uiContext, "Radiator Fan", "radiator_fan.jpg", TEST, CMD_FAN_BENCH);
        return panel.getContent();
    }

    private Component createAcRelayTest() {
        CommandControl panel = new FixedCommandControl(uiContext, "A/C Compressor Relay", ".jpg", TEST, CMD_AC_RELAY_BENCH);
        return panel.getContent();
    }

    private Component createFuelPumpTest() {
        CommandControl panel = new FixedCommandControl(uiContext, "Fuel Pump", "fuel_pump.jpg", TEST, "fuelpumpbench");
        return panel.getContent();
    }

    private Component createSparkTest() {
        final JComboBox<Integer> indexes = createIndexCombo(Fields.MAX_CYLINDER_COUNT);
        CommandControl panel = new CommandControl(uiContext,"Spark #", "spark.jpg", TEST, indexes) {
            @Override
            protected String getCommand() {
                return "sparkbench2 1000 " + indexes.getSelectedItem() + " 5 333 3";
            }
        };
        return panel.getContent();
    }

    private Component createInjectorTest() {
        final JComboBox<Integer> indexes = createIndexCombo(Fields.MAX_CYLINDER_COUNT);
        CommandControl panel = new CommandControl(uiContext,"Injector #", "injector.png", TEST, indexes) {
            @Override
            protected String getCommand() {
                return "fuelbench2 1000 " + indexes.getSelectedItem() + " 5 333 3";
            }
        };
        return panel.getContent();
    }

    @NotNull
    private JComboBox<Integer> createIndexCombo(Integer count) {
        JComboBox<Integer> indexes = new JComboBox<>();
        for (int i = 1; i <= count; i++) {
            indexes.addItem(i);
        }
        return indexes;
    }

    public JPanel getContent() {
        return content;
    }

}
