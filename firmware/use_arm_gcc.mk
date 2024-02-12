UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	TRGT = $(PROJECT_DIR)/ext/compiler-experiment/arm-gnu-toolchain-11.3.rel1-darwin-x86_64-arm-none-eabi/bin/arm-none-eabi-
else
	TRGT = $(PROJECT_DIR)/ext/compiler-experiment/arm-gnu-toolchain-11.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-
endif

$(info TRGT:          $(TRGT))
