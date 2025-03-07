package com.rusefi.ui.util;

import com.devexperts.logging.Logging;

import javax.swing.*;
import java.awt.*;
import java.io.ByteArrayOutputStream;
import java.io.PrintStream;

import static com.devexperts.logging.Logging.getLogging;
import static com.rusefi.Launcher.*;

/**
 * 6/30/13
 * Andrey Belomutskiy, (c) 2013-2020
 */
public class DefaultExceptionHandler implements Thread.UncaughtExceptionHandler {
    private static final Logging log = getLogging(DefaultExceptionHandler.class);

    private static boolean hadExceptionAlready;

    public void uncaughtException(Thread t, Throwable e) {
        handleException(e);
    }

    public static void handleException(Throwable e) {
        if (e == null) {
            log.info("Null exception?");
            throw new NullPointerException("Throwable e");
        }
        e.printStackTrace(); // output to error log
        if (hadExceptionAlready)
            return;
        hadExceptionAlready = true;

        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        PrintStream ps = new PrintStream(baos);
        e.printStackTrace(ps);

        JPanel content = new JPanel(new BorderLayout());
        content.add(new JLabel(e.toString()), BorderLayout.NORTH);

        JTextArea textArea = new JTextArea(baos.toString());

        textArea.setLineWrap(true);
        textArea.setWrapStyleWord(true);
        JScrollPane scrollPane = new JScrollPane(textArea) {
            @Override
            public Dimension getPreferredSize() {
                return new Dimension(500, 400);
            }
        };

        content.add(scrollPane, BorderLayout.CENTER);

        JOptionPane.showConfirmDialog(findActiveFrame(), content, CONSOLE_VERSION + ": Exception Occurred", JOptionPane.DEFAULT_OPTION);
        log.info("handleException: " + baos);
    }

    private static Frame findActiveFrame() {
        Frame[] frames = JFrame.getFrames();
        for (Frame frame : frames) {
            if (frame.isVisible())
                return frame;
        }
        return null;
    }
}
