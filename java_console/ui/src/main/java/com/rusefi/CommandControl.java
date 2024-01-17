package com.rusefi;

import com.rusefi.ui.UIContext;
import com.rusefi.ui.util.UiUtils;
import org.jetbrains.annotations.NotNull;
import org.putgemin.VerticalFlowLayout;

import javax.swing.*;
import java.awt.*;
import java.awt.event.ActionListener;

/**
 * A control which sends specific command defined by @link {@link #getCommand()} method
 *
 * @see com.rusefi.ui.widgets.AnyCommand for free type command control
 * Andrey Belomutskiy, (c) 2013-2020
 */
abstract class CommandControl {
    public static final String TEST = "Test";
    protected final JPanel panel = new JPanel(new FlowLayout(FlowLayout.RIGHT, 5, 0));
    private final UIContext uiContext;

    public CommandControl(UIContext uiContext, String labelText, String iconFileName, String buttonText, JComponent... components) {
        this.uiContext = uiContext;
        ImageIcon icon = UiUtils.loadIcon(iconFileName);
        JPanel rightVerticalPanel = new JPanel(new VerticalFlowLayout());
        rightVerticalPanel.add(new JLabel(labelText));
        for (JComponent component : components)
            rightVerticalPanel.add(component);
        JButton button = new JButton(buttonText);
        rightVerticalPanel.add(button);

        panel.add(new JLabel(icon));
        panel.add(rightVerticalPanel);

        int GAP = 3;

        panel.setBorder(BorderFactory.createCompoundBorder(BorderFactory.createLineBorder(Color.black), BorderFactory.createEmptyBorder(GAP, GAP, GAP, GAP)));

        button.addActionListener(createButtonListener());
    }

    @NotNull
    protected ActionListener createButtonListener() {
        return e -> uiContext.getCommandQueue().write(getCommand());
    }

    protected abstract String getCommand();

    public Component getContent() {
        return UiUtils.wrap(panel);
    }
}
