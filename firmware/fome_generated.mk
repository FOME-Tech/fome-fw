# Stamp file tracks when generation last ran
GENERATED_STAMP := $(BUILDDIR)/generated.stamp

# Generated files list - populated by included .mk files from Java tools
GENERATED_FILES :=
-include $(PROJECT_DIR)/.dep/generated_live_data_outputs.mk
-include $(PROJECT_DIR)/.dep/generated_config_outputs.mk

# Generation rule uses stamp file as target
$(GENERATED_STAMP) : $(PROJECT_DIR)/integration/fome_config.txt
	@echo Generating config files...
	cd $(PROJECT_DIR) && ./gen_live_documentation.sh $(GENERATED_STAMP)
	cd $(PROJECT_DIR) && ./gen_config_board.sh $(realpath $(BOARD_DIR)) $(SHORT_BOARD_NAME) $(GENERATED_STAMP)
	@touch $@

# Generated files depend on stamp (prevents orphaned file issues)
$(GENERATED_FILES) : $(GENERATED_STAMP)

# All c/c++ objects depend on generated stamp
$(OBJS) : $(GENERATED_STAMP)
$(PCHOBJ) : $(GENERATED_STAMP)

JAVA_TOOLS = $(BUILDDIR)/java_tools.cookie

$(JAVA_TOOLS) :
	cd $(PROJECT_DIR)/../java_tools && ./build_tools.sh
	echo "done" > $@

# Generated files depend on the generation tools
$(GENERATED_STAMP) : $(JAVA_TOOLS)

CLEAN_GENERATED_HOOK:
	rm -f $(GENERATED_DIR)/*
	rm -f $(GENERATED_STAMP)
	git checkout -- $(PROJECT_DIR)/hw_layer/mass_storage/ramdisk_image.h
	git checkout -- $(PROJECT_DIR)/hw_layer/mass_storage/ramdisk_image_compressed.h
