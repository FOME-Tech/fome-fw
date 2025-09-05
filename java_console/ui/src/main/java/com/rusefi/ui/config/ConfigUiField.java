package com.rusefi.ui.config;

import com.opensr5.ConfigurationImage;
import com.rusefi.config.Field;
import com.rusefi.config.FieldCommandResponse;
import com.rusefi.core.MessagesCentral;
import com.rusefi.core.Pair;
import com.rusefi.ui.UIContext;
import com.rusefi.ui.util.JTextFieldWithWidth;

import javax.swing.*;
import java.awt.event.KeyAdapter;
import java.awt.event.KeyEvent;

public class ConfigUiField extends BaseConfigField {
    private final JTextField view = new JTextFieldWithWidth(200);

    public ConfigUiField(UIContext uiContext, final Field field, String topLabel) {
        super(uiContext, field);
        createUi(topLabel, view);
        requestInitialValue(); // this is not in base constructor so that view is created by the time we invoke it

        MessagesCentral.getInstance().addListener(new MessagesCentral.MessageListener() {
            @Override
            public void onMessage(Class clazz, String message) {
                if (FieldCommandResponse.isIntValueMessage(message) || FieldCommandResponse.isFloatValueMessage(message)) {
                    Pair<Integer, ?> p = FieldCommandResponse.parseResponse(message);
                    if (p != null && p.first == field.getOffset()) {
                        Object value = p.second;
                        setValue(value);
                    }
                }
            }
        });

        view.addKeyListener(new KeyAdapter() {
            @Override
            public void keyPressed(KeyEvent e) {
                if (e.getKeyCode() == KeyEvent.VK_ENTER) {
                    sendValue(field, ConfigUiField.this.view.getText());
                }
            }
        });
    }

    private void setValue(Object value) {
        view.setEnabled(true);
        view.setText("" + value);
        onValueArrived();
    }

    @Override
    protected void loadValue(ConfigurationImage ci) {
        Number value = field.getValue(ci);
        setValue(value);
    }
}