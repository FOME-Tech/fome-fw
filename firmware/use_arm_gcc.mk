# Try to find arm-none-eabi-gcc 
ifndef $(shell command -v arm-none-eabi-gcc 2> /dev/null)
# No compiler found, use the one from submodule
	TRGT = $(PROJECT_DIR)/ext/compiler-experiment/arm-gnu-toolchain-11.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-
else
# Use the compiler already in $PATH
	TRGT = arm-none-eabi-
endif

$(info TRGT is $(TRGT))
