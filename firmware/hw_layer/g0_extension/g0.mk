ifeq ($(G0_EXTENSION),yes)
DDEFS += -DEFI_G0_EXTENSION=TRUE -DBOARD_G0_GPIOCHIP_COUNT=2
HW_LAYER_EMS_CPP += \
	$(PROJECT_DIR)/hw_layer/g0_extension/link.cpp \
	$(PROJECT_DIR)/hw_layer/g0_extension/service.cpp \
	$(PROJECT_DIR)/hw_layer/g0_extension/analog_provider.cpp \
	$(PROJECT_DIR)/hw_layer/g0_extension/digital_gpio.cpp \
	$(PROJECT_DIR)/hw_layer/g0_extension/output_gpio.cpp
endif
