ifeq ($(G0_EXTENSION),yes)
DDEFS += -DEFI_G0_EXTENSION=TRUE
HW_LAYER_EMS_CPP += \
	$(PROJECT_DIR)/hw_layer/g0_extension/link.cpp \
	$(PROJECT_DIR)/hw_layer/g0_extension/service.cpp
endif
