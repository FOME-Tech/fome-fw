# Real MCU callers and portable ChibiOS HAL with host peripheral mocks.
# Separate binaries/PCH from the engine tests, for the two HAL register layouts.
ADC_LIFECYCLE_DIR := adc_lifecycle
ADC_LIFECYCLE_BINS := build/adc_lifecycle_test build/adc_lifecycle_v4_test
ifeq ($(SANITIZE),yes)
ADC_LIFECYCLE_SANITIZE := -fsanitize=address,undefined
endif
ADC_LIFECYCLE_INC := -I$(ADC_LIFECYCLE_DIR)/mocks \
	-I$(PROJECT_DIR)/ext/ChibiOS/os/hal/include \
	-I$(PROJECT_DIR)/hw_layer/adc \
	-I$(UNIT_TESTS_DIR)/googletest/googletest/include \
	-I$(UNIT_TESTS_DIR)/googletest/googletest

.PHONY: adc-lifecycle-test
adc-lifecycle-test: $(ADC_LIFECYCLE_BINS)
	./build/adc_lifecycle_test
	./build/adc_lifecycle_v4_test

build/adc_lifecycle_test: ADC_LIFECYCLE_PORT := ADCv2
build/adc_lifecycle_v4_test: ADC_LIFECYCLE_PORT := ADCv4
build/adc_lifecycle_v4_test: ADC_LIFECYCLE_DEFINES := -DADC_TEST_V4=1

$(ADC_LIFECYCLE_BINS): $(ADC_LIFECYCLE_DIR)/test_adc_lifecycle.cpp \
	$(ADC_LIFECYCLE_DIR)/adc_lifecycle.mk \
	$(PROJECT_DIR)/controllers/sensors/impl/software_knock.h \
	$(PROJECT_DIR)/config/boards/f407-discovery/knock_config.h \
	$(PROJECT_DIR)/config/boards/proteus/knock_config.h \
	$(wildcard $(ADC_LIFECYCLE_DIR)/mocks/*) \
	$(PROJECT_DIR)/hw_layer/ports/stm32/stm32_adc_v2.cpp \
	$(PROJECT_DIR)/hw_layer/ports/stm32/stm32_adc_v4.cpp \
	$(PROJECT_DIR)/hw_layer/adc/adc_diagnostics.cpp \
	$(PROJECT_DIR)/hw_layer/adc/adc_diagnostics.h \
	$(PROJECT_DIR)/controllers/sensors/impl/software_knock.cpp \
	$(PROJECT_DIR)/ext/ChibiOS/os/hal/src/hal_adc.c \
	$(PROJECT_DIR)/ext/ChibiOS/os/hal/include/hal_adc.h \
	$(PROJECT_DIR)/ext/ChibiOS/os/hal/ports/STM32/LLD/ADCv2/hal_adc_lld.h \
	$(PROJECT_DIR)/ext/ChibiOS/os/hal/ports/STM32/LLD/ADCv4/hal_adc_lld.h | $(BUILDDIR)
	$(CPPC) -std=c++20 -g -O0 -Wall -Wextra -Wno-unused-parameter \
		$(ADC_LIFECYCLE_SANITIZE) $(ADC_LIFECYCLE_INC) $(ADC_LIFECYCLE_DEFINES) \
		-I$(PROJECT_DIR)/ext/ChibiOS/os/hal/ports/STM32/LLD/$(ADC_LIFECYCLE_PORT) $< \
		$(UNIT_TESTS_DIR)/googletest/googletest/src/gtest-all.cc \
		$(UNIT_TESTS_DIR)/googletest/googletest/src/gtest_main.cc -pthread -o $@
