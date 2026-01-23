package com.rusefi;

import com.devexperts.logging.Logging;
import com.opensr5.ini.IniFileModel;
import com.rusefi.core.io.BundleUtil;
import com.rusefi.io.serial.BaudRateHolder;
import com.rusefi.maintenance.ProgramSelector;
import com.rusefi.ui.util.HorizontalLine;
import com.rusefi.ui.util.UiUtils;
import com.rusefi.util.IoUtils;
import net.miginfocom.swing.MigLayout;
import org.jetbrains.annotations.Nullable;
import org.putgemin.VerticalFlowLayout;

import javax.swing.*;
import javax.swing.border.TitledBorder;
import java.awt.*;
import java.awt.event.*;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static com.devexperts.logging.Logging.getLogging;
import static com.rusefi.core.preferences.storage.PersistentConfiguration.getConfig;
import static com.rusefi.ui.util.UiUtils.*;

/**
 * This frame is used on startup to select the port we would be using
 *
 * @author Andrey Belomutskiy
 * <p/>
 * 2/14/14
 * @see SimulatorHelper
 */
public class StartupFrame {
    private static final Logging log = getLogging(Launcher.class);

    public static final String LOGO_PATH = "/com/rusefi/";
    private static final String LOGO = LOGO_PATH + "logo.png";
    // private static final int RUSEFI_ORANGE = 0xff7d03;

    private final JFrame frame;
    private final JPanel connectPanel = new JPanel(new FlowLayout());
    // todo: move this line to the connectPanel
    private final JComboBox<SerialPortScanner.PortResult> comboPorts = new JComboBox<>();
    private final JPanel leftPanel = new JPanel(new VerticalFlowLayout());

    private final JPanel realHardwarePanel = new JPanel(new MigLayout());
    private final JPanel miscPanel = new JPanel(new MigLayout()) {
        @Override
        public Dimension getPreferredSize() {
            // want miscPanel and realHardwarePanel to be the same width
            Dimension size = super.getPreferredSize();
            return new Dimension(Math.max(size.width, realHardwarePanel.getPreferredSize().width), size.height);
        }
    };
    /**
     * this flag tells us if we are closing the startup frame in order to proceed with console start or if we are
     * closing the application.
     */
    private boolean isProceeding;
    private final JLabel statusMessage = new JLabel();
    private JButton connectButton;
    private SerialPortScanner.AvailableHardware currentHardware;

    public StartupFrame() {
        frame = new JFrame("FOME Console");
        frame.setDefaultCloseOperation(JDialog.DISPOSE_ON_CLOSE);
        frame.addWindowListener(new WindowAdapter() {
            @Override
            public void windowClosed(WindowEvent ev) {
                if (!isProceeding) {
                    getConfig().save();
                    IoUtils.exit("windowClosed", 0);
                }
            }
        });
        frame.setResizable(false);
        UiUtils.setAppIcon(frame);

        // Attempt to drop our ini in to the TS cache
        updateTsIniCache();
    }

    public void chooseSerialPort() {
        realHardwarePanel.setBorder(new TitledBorder(BorderFactory.createLineBorder(Color.darkGray), "Real stm32"));
        miscPanel.setBorder(new TitledBorder(BorderFactory.createLineBorder(Color.darkGray), "Miscellaneous"));

        connectPanel.add(comboPorts);
        final JComboBox<String> comboSpeeds = createSpeedCombo();
        connectPanel.add(comboSpeeds);

        connectButton = new JButton("Connect", new ImageIcon(getClass().getResource("/com/rusefi/connect48.png")));
        setToolTip(connectButton, "Connect to real hardware");
        connectPanel.add(connectButton);
        connectPanel.setVisible(false);

        // Update connect button state when port selection changes
        comboPorts.addItemListener(e -> updateConnectButtonState());

        frame.getRootPane().setDefaultButton(connectButton);
        connectButton.addKeyListener(new KeyAdapter() {
            @Override
            public void keyPressed(KeyEvent e) {
                if (e.getKeyCode() == KeyEvent.VK_ENTER) {
                    connectButtonAction(comboSpeeds);
                }
            }
        });

        connectButton.addActionListener(e -> connectButtonAction(comboSpeeds));

        leftPanel.add(realHardwarePanel);
        leftPanel.add(miscPanel);

        realHardwarePanel.add(connectPanel, "right, wrap");
        realHardwarePanel.add(statusMessage, "right, wrap");

        ProgramSelector selector = new ProgramSelector(comboPorts);

        realHardwarePanel.add(new HorizontalLine(), "right, wrap");

        realHardwarePanel.add(selector.getControl(), "right, wrap");

        if (FileLog.isWindows()) {
            // for F7 builds we just build one file at the moment
//            realHardwarePanel.add(new FirmwareFlasher(FirmwareFlasher.IMAGE_FILE, "ST-LINK Program Firmware", "Default firmware version for most users").getButton());

            JComponent updateHelp = ProgramSelector.createHelpButton();

            realHardwarePanel.add(updateHelp, "right, wrap");

            // st-link is pretty advanced use-case, real humans do not have st-link as of 2021
            //realHardwarePanel.add(new EraseChip().getButton(), "right, wrap");
        }

        SerialPortScanner.INSTANCE.addListener(currentHardware -> SwingUtilities.invokeLater(() -> {
            selector.apply(currentHardware);
            applyKnownPorts(currentHardware);
            frame.pack();
        }));

        miscPanel.add(new HorizontalLine(), "wrap");

        miscPanel.add(SimulatorHelper.createSimulatorComponent(this));

        JPanel rightPanel = new JPanel(new VerticalFlowLayout());

        JLabel logo = createLogoLabel();
        if (logo != null)
            rightPanel.add(logo);
        rightPanel.add(new JLabel("FOME (c) 2023-2025"));
        rightPanel.add(new JLabel("rusEFI (c) 2012-2023"));
        rightPanel.add(new JLabel(BundleUtil.readBundleFullNameNotNull()));

        JPanel content = new JPanel(new BorderLayout());
        content.add(leftPanel, BorderLayout.WEST);
        content.add(rightPanel, BorderLayout.EAST);
        frame.add(content);
        frame.pack();
        setFrameIcon(frame);
        frame.setVisible(true);
        UiUtils.centerWindow(frame);
    }

    private void applyKnownPorts(SerialPortScanner.AvailableHardware hardware) {
        this.currentHardware = hardware;
        List<SerialPortScanner.PortResult> ports = hardware.getKnownPorts();
        log.info("Rendering available ports: " + ports);

        applyPortSelectionToUIcontrol(ports);

        // Update UI visibility and status message based on what hardware is detected
        boolean hasAnyPorts = !ports.isEmpty();
        connectPanel.setVisible(hasAnyPorts);

        updateConnectButtonState();

        UiUtils.trueLayout(connectPanel);
    }

    private void updateConnectButtonState() {
        if (connectButton == null) {
            return;
        }

        boolean dfuFound = currentHardware != null && currentHardware.dfuFound;
        SerialPortScanner.PortResult selectedPort = (SerialPortScanner.PortResult) comboPorts.getSelectedItem();

        if (selectedPort == null) {
            // No serial ports available
            connectButton.setEnabled(false);
            connectButton.setToolTipText("No port selected");

            if (dfuFound) {
                statusMessage.setText("<html>DFU device detected.<br>Use firmware update options below.</html>");
            } else {
                statusMessage.setText("<html>No ports found!<br>Confirm blue LED is blinking</html>");
            }
            return;
        }

        boolean canConnect = selectedPort.isEcu();
        connectButton.setEnabled(canConnect);

        // Build status message - port type info, plus DFU if present
        String portStatus;
        if (canConnect) {
            connectButton.setToolTipText("Connect to ECU at " + selectedPort.port);
            portStatus = null;
        } else if (selectedPort.type == SerialPortScanner.SerialPortType.OpenBlt) {
            connectButton.setToolTipText("Cannot connect - device is in bootloader mode");
            portStatus = "Bootloader mode - use Update Firmware below";
        } else {
            connectButton.setToolTipText("Cannot connect to unknown device");
            portStatus = "Unknown device type";
        }

        // Combine port status with DFU status if both present
        if (portStatus != null && dfuFound) {
            statusMessage.setText("<html>" + portStatus + "<br>DFU device also detected.</html>");
        } else if (portStatus != null) {
            statusMessage.setText("<html>" + portStatus + "</html>");
        } else if (dfuFound) {
            statusMessage.setText("<html>DFU device also detected.</html>");
        } else {
            statusMessage.setText("");
        }
    }

    public static void setFrameIcon(Frame frame) {
        ImageIcon icon = getBundleIcon();
        if (icon != null)
            frame.setIconImage(icon.getImage());
    }

    public static JLabel createLogoLabel() {
        ImageIcon logoIcon = getBundleIcon();
        if (logoIcon == null)
            return null;
        JLabel logo = new JLabel(logoIcon);
        logo.setBorder(BorderFactory.createEmptyBorder(0, 0, 0, 10));
        return logo;
    }

    @Nullable
    private static ImageIcon getBundleIcon() {
        String bundle = BundleUtil.readBundleFullNameNotNull();
        String logoName;
        // these should be about 213px wide
        if (bundle.contains("proteus")) {
            logoName = LOGO_PATH + "logo_proteus.png";
        } else if (bundle.contains("_alphax")) {
            logoName = LOGO_PATH + "logo_alphax.png";
        } else {
            logoName = LOGO;
        }
        return UiUtils.loadIcon(logoName);
    }

    private void connectButtonAction(JComboBox<String> comboSpeeds) {
        BaudRateHolder.INSTANCE.baudRate = Integer.parseInt((String) comboSpeeds.getSelectedItem());
        SerialPortScanner.PortResult selectedPort = ((SerialPortScanner.PortResult)comboPorts.getSelectedItem());

        if (selectedPort == null) {
            return;
        }

        // Ensure that the bundle matches between the controller and console
        if (selectedPort.signature != null && !selectedPort.signature.matchesBundle()) {
            String target = BundleUtil.getBundleTarget();
            String message = String.format(
                    "Looks like you're using the wrong console bundle for your controller.\nYou can attempt to proceed, but unexpected behavior may result.\nContinue at your own risk.\n\nController: %s\nBundle: %s",
                    selectedPort.signature.getBundleTarget(),
                    target
                );

            int result = JOptionPane.showConfirmDialog(this.frame, message, "WARNING", JOptionPane.OK_CANCEL_OPTION);

            if (result != JOptionPane.OK_OPTION) {
                return;
            }
        }

        disposeFrameAndProceed();
        new ConsoleUI(selectedPort.port);
    }

    public void disposeFrameAndProceed() {
        isProceeding = true;
        frame.dispose();
        SerialPortScanner.INSTANCE.stopTimer();
    }

    private void applyPortSelectionToUIcontrol(List<SerialPortScanner.PortResult> ports) {
        comboPorts.removeAllItems();
        for (final SerialPortScanner.PortResult port : ports) {
            comboPorts.addItem(port);
        }

        String defaultPort = getConfig().getRoot().getProperty(ConsoleUI.PORT_KEY);
        comboPorts.setSelectedItem(defaultPort);
        trueLayout(comboPorts);
    }

    private static JComboBox<String> createSpeedCombo() {
        JComboBox<String> combo = new JComboBox<>();
        String defaultSpeed = getConfig().getRoot().getProperty(ConsoleUI.SPEED_KEY, "115200");
        for (int speed : new int[]{9600, 14400, 19200, 38400, 57600, 115200, 460800, 921600})
            combo.addItem(Integer.toString(speed));
        combo.setSelectedItem(defaultSpeed);
        return combo;
    }

    private static void updateTsIniCache() {
        new Thread(() -> {
            try {
                File cacheDir = new File(System.getProperty("user.home"), ".efiAnalytics/TunerStudio/config/ecuDef");

                if (!cacheDir.exists() || !cacheDir.canWrite()) {
                    log.warn("TS ini cache prime failed, cache dir does not exist or not writable: " + cacheDir);
                    return;
                }

                String signature = IniFileModel.getInstance().getSignature();
                // Replace spaces, punctuation, then add the ini file extension
                String destFileName = signature.replaceAll("[() ]+", "_") + ".ini";

                File dest = new File(cacheDir, destFileName);

                // If the file already exists, skip it
                if (dest.exists()) {
                    log.info("TS ini cache prime skipped, target already exists: " + dest.getCanonicalPath());
                    return;
                }

                Path sourcePath = IniFileModel.getInstance().getSourceFile().toPath();
                Path destPath = dest.toPath();

                // Copy source -> dest
                Files.copy(sourcePath, dest.toPath());

                log.info("Successfully primed TS ini cache " + sourcePath + " -> " + destPath);
            } catch (IOException e) {
                log.error("Failed to prime TS ini cache", e);
            }
        }).start();
    }
}
