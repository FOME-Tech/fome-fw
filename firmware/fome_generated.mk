GENERATED := \
	$(GENERATED_DIR)/engine_configuration_generated_structures.h \
	$(GENERATED_DIR)/live_data_fragments.h \
	$(GENERATED_DIR)/live_data_ids.h \
	$(GENERATED_DIR)/log_fields_generated.h \
	$(GENERATED_DIR)/output_lookup_generated.cpp \
	$(GENERATED_DIR)/rusefi_generated.h \
	$(GENERATED_DIR)/value_lookup_generated.cpp

$(GENERATED) : $(PROJECT_DIR)/integration/rusefi_config.txt
	@echo Generating $(<F)
	./gen_live_documentation.sh
	./gen_config_board.sh $(BOARD_DIR) $(SHORT_BOARD_NAME)

# PCH depends on generated files
$(PCHOBJ) : $(GENERATED)
