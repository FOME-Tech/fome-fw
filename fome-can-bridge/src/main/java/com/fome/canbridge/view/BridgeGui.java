package com.fome.canbridge.view;

import com.rusefi.ui.StatusConsumer;
import net.miginfocom.swing.MigLayout;
import javax.swing.*;
import javax.swing.border.EmptyBorder;
import java.awt.*;
import java.awt.event.ActionEvent;
import java.util.function.Consumer;

public class BridgeGui extends JFrame implements StatusConsumer {
    private final JTextArea logArea = new JTextArea();
    private final JComboBox<String> deviceCombo = new JComboBox<>(new String[]{"can0"});
    private final JButton scanButton = new JButton("Scan");
    private final JTextField txIdField = new JTextField("0x100");
    private final JTextField rxIdField = new JTextField("0x102");
    private final JComboBox<String> typeCombo = new JComboBox<>(new String[]{"SLCAN", "SocketCAN"});

    private final JButton connectButton = new JButton("Connect Bridge");
    private final JButton helpButton = new JButton("Help & Guide");

    private Consumer<BridgeConfig> onConnect;

    public static class BridgeConfig {
        public String type;
        public String device;
        public int txId;
        public int rxId;
    }

    public BridgeGui() {
        super("FOME CAN Bridge");
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        setSize(600, 500);
        setLayout(new BorderLayout());

        JPanel mainPanel = new JPanel(new MigLayout("fillx, insets 20", "[right]10[grow]5[right]10[right]", "[]10[]10[]10[]10[]10[]20[]"));
        mainPanel.setBorder(new EmptyBorder(10, 10, 10, 10));

        // Header
        JLabel header = new JLabel("Connection Settings");
        header.setFont(header.getFont().deriveFont(Font.BOLD, 16f));
        mainPanel.add(header, "span 2, wrap, gapbottom 15");

        // Type
        mainPanel.add(new JLabel("Adapter Type:"));
        mainPanel.add(typeCombo, "growx");
        mainPanel.add(createHelpButton("SocketCAN: standard Linux driver.\nSLCAN: STM32 Virtual COM port, CANable, etc."), "wrap");

        // Device
        mainPanel.add(new JLabel("Device Name:"));
        deviceCombo.setEditable(true);
        mainPanel.add(deviceCombo, "growx");
        mainPanel.add(scanButton);
        mainPanel.add(createHelpButton("Linux: can0, can1, etc.\nWindows COM Ports: COM1, COM2, etc.\nClick 'Scan' to find connected devices."), "wrap");


        // TX ID
        mainPanel.add(new JLabel("PC -> ECU ID (TX):"));
        mainPanel.add(txIdField, "growx");
        mainPanel.add(createHelpButton("The CAN ID that TunerStudio will use to send commands. Default: 0x100."), "wrap");

        // RX ID
        mainPanel.add(new JLabel("ECU -> PC ID (RX):"));
        mainPanel.add(rxIdField, "growx");
        mainPanel.add(createHelpButton("The CAN ID that the ECU will use to send data back. Default: 0x102."), "wrap");

        // Buttons
        JPanel btnPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT));
        btnPanel.add(helpButton);
        btnPanel.add(connectButton);
        mainPanel.add(btnPanel, "span 3, growx, gaptop 20");

        // Log Area
        logArea.setEditable(false);
        logArea.setBackground(new Color(30, 30, 30));
        logArea.setForeground(new Color(100, 200, 100));
        logArea.setFont(new Font("Monospaced", Font.PLAIN, 12));
        logArea.setBorder(new EmptyBorder(5, 5, 5, 5));
        JScrollPane scroll = new JScrollPane(logArea);
        scroll.setBorder(BorderFactory.createTitledBorder("System Log"));

        add(mainPanel, BorderLayout.NORTH);
        add(scroll, BorderLayout.CENTER);

        connectButton.addActionListener(this::handleConnect);
        helpButton.addActionListener(e -> showGeneralHelp());
        scanButton.addActionListener(e -> handleScan());
        typeCombo.addActionListener(e -> handleScan()); // Auto-scan on type change
        
        setLocationRelativeTo(null);
        handleScan(); // Initial scan
    }

    private void handleScan() {
        String type = (String) typeCombo.getSelectedItem();
        new Thread(() -> {
            java.util.List<String> devices = new java.util.ArrayList<>();
            if ("SLCAN".equalsIgnoreCase(type)) {
                try {
                    for (com.fazecast.jSerialComm.SerialPort port : com.fazecast.jSerialComm.SerialPort.getCommPorts()) {
                        devices.add(port.getSystemPortName());
                    }
                } catch (Exception e) {
                    devices.add("COM1");
                }
            } else {
                devices.add("can0");
            }
            SwingUtilities.invokeLater(() -> {
                deviceCombo.removeAllItems();
                for (String d : devices) deviceCombo.addItem(d);
            });
        }).start();
    }

    private JButton createHelpButton(String tip) {
        JButton btn = new JButton("?");
        btn.setMargin(new Insets(2, 6, 2, 6));
        btn.setToolTipText(tip);
        btn.addActionListener(e -> JOptionPane.showMessageDialog(this, tip, "Parameter Help", JOptionPane.INFORMATION_MESSAGE));
        return btn;
    }

    private void showGeneralHelp() {
        String helpText = "<html><body style='width: 300px;'>" +
                "<h2>FOME CAN Bridge Guide</h2>" +
                "<p>This tool allows <b>TunerStudio</b> to connect to your ECU over the <b>CAN bus</b> instead of USB/Serial.</p>" +
                "<h3>Requirements:</h3>" +
                "<ul>" +
                "<li><b>Linux:</b> SocketCAN (can0) or SLCAN (/dev/ttyACM0).</li>" +
                "<li><b>Windows:</b> SLCAN (COM port, e.g. CANable).</li>" +
                "<li><b>Firmware:</b> Yellowbox firmware with 'TS over CAN' enabled.</li>" +
                "</ul>" +
                "<h3>How to use:</h3>" +
                "<ol>" +
                "<li>Configure the IDs to match your ECU settings.</li>" +
                "<li>Click 'Connect Bridge'.</li>" +
                "<li>In TunerStudio, select <b>Connection Type: TCP/IP</b>.</li>" +
                "<li>Set Server: <b>localhost</b> and Port: <b>29001</b>.</li>" +
                "</ol>" +
                "</body></html>";
        JOptionPane.showMessageDialog(this, helpText, "FOME CAN Bridge - General Help", JOptionPane.PLAIN_MESSAGE);
    }

    private void handleConnect(ActionEvent e) {
        if (onConnect != null) {
            try {
                BridgeConfig config = new BridgeConfig();
                config.type = (String) typeCombo.getSelectedItem();
                config.device = (String) deviceCombo.getSelectedItem();

                config.txId = Integer.decode(txIdField.getText());
                config.rxId = Integer.decode(rxIdField.getText());
                
                connectButton.setEnabled(false);
                logLine("Starting connection...");
                new Thread(() -> onConnect.accept(config)).start();
            } catch (Exception ex) {
                logLine("Error: " + ex.getMessage());
            }
        }
    }

    @Override
    public void logLine(String status) {
        SwingUtilities.invokeLater(() -> {
            logArea.append(status + "\n");
            logArea.setCaretPosition(logArea.getDocument().getLength());
        });
    }

    public void setOnConnect(Consumer<BridgeConfig> onConnect) {
        this.onConnect = onConnect;
    }

    public void setConnectEnabled(boolean enabled) {
        SwingUtilities.invokeLater(() -> connectButton.setEnabled(enabled));
    }
}
