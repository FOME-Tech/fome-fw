DRIVERS_DIR=$(PROJECT_DIR)/hw_layer/drivers

HW_LAYER_DRIVERS_INC = \
	$(DRIVERS_DIR) \
	$(DRIVERS_DIR)/gpio \
	$(DRIVERS_DIR)/can \
	$(DRIVERS_DIR)/sent \
	$(DRIVERS_DIR)/serial \
	$(DRIVERS_DIR)/i2c

HW_LAYER_DRIVERS_CORE = \

HW_LAYER_DRIVERS_CORE_CPP = \
	$(DRIVERS_DIR)/gpio/core.cpp \
	$(DRIVERS_DIR)/sent/sent.cpp \
	$(DRIVERS_DIR)/i2c/i2c_bb.cpp \
	$(DRIVERS_DIR)/can/can_msg_tx.cpp

HW_LAYER_DRIVERS =

HW_LAYER_DRIVERS_CPP = \
	$(DRIVERS_DIR)/can/can_hw.cpp \
	$(DRIVERS_DIR)/can/can_config.cpp \
	$(DRIVERS_DIR)/serial/serial_hw.cpp \
	$(DRIVERS_DIR)/gpio/tle6240.cpp \
	$(DRIVERS_DIR)/gpio/tle8888.cpp \
	$(DRIVERS_DIR)/gpio/mc33972.cpp \
	$(DRIVERS_DIR)/gpio/mc33810.cpp \
	$(DRIVERS_DIR)/gpio/drv8860.cpp \
	$(DRIVERS_DIR)/gpio/tle9104.cpp \
	$(DRIVERS_DIR)/gpio/l9779.cpp \
	$(DRIVERS_DIR)/gpio/protected_gpio.cpp \
	$(DRIVERS_DIR)/sent/sent_hw_icu.cpp \
