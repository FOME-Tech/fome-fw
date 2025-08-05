GENERATED := \
	$(GENERATED_DIR)/engine_configuration_generated_structures.h \
# $(GENERATED_DIR)/live_data_fragments.h \
# $(GENERATED_DIR)/live_data_ids.h \
# $(GENERATED_DIR)/log_fields_generated.h \
# $(GENERATED_DIR)/output_lookup_generated.cpp \
# $(GENERATED_DIR)/rusefi_generated.h \
# $(GENERATED_DIR)/value_lookup_generated.cpp
# TODO: how do we list multiple dependencies without the build happening multiple times?

$(GENERATED) : $(PROJECT_DIR)/integration/rusefi_config.txt
	@echo Generating config files...
	cd $(PROJECT_DIR) && ./gen_live_documentation.sh
	cd $(PROJECT_DIR) && ./gen_config_board.sh $(BOARD_DIR) $(SHORT_BOARD_NAME)

# All c/c++ objects depend on generated
$(OBJS) : $(GENERATED)
$(PCHOBJ) : $(GENERATED)

JAVA_TOOLS = $(BUILDDIR)/java_tools.cookie

$(JAVA_TOOLS) :
	cd $(PROJECT_DIR)/../java_tools && ./build_tools.sh
	echo "done" > $@

# Generated files depend on the generation tools
$(GENERATED) : $(JAVA_TOOLS)

CLEAN_GENERATED_HOOK:
	rm -f $(GENERATED_DIR)/*
	git checkout -- $(PROJECT_DIR)/hw_layer/mass_storage/ramdisk_image.h
	git checkout -- $(PROJECT_DIR)/hw_layer/mass_storage/ramdisk_image_compressed.h
