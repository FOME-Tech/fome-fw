UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	COMPILER_PLATFORM = arm-gnu-toolchain-11.3.rel1-darwin-x86_64-arm-none-eabi
else
	COMPILER_PLATFORM = arm-gnu-toolchain-11.3.rel1-x86_64-arm-none-eabi
endif

TRGT = $(PROJECT_DIR)/ext/build-tools/$(COMPILER_PLATFORM)/bin/arm-none-eabi-

$(info COMPILER_PLATFORM: $(COMPILER_PLATFORM))
$(info TRGT:              $(TRGT))
