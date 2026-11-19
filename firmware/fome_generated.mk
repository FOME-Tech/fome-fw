# Code generation runs in two stages with independent stamp files:
#   1. live data   (gen_live_documentation.sh -> LiveDataProcessor)
#   2. main config (gen_config_board.sh        -> ConfigDefinition), which consumes
#      generated/total_live_data_generated.h and the output_channels/data_logs ini
#      produced by stage 1, hence config depends on the live-data stamp.
# Splitting the stamps keeps the dependency graph acyclic and lets each stage rerun
# independently when only its own inputs change.
LIVE_DATA_STAMP := $(BUILDDIR)/generated_live_data.stamp
CONFIG_STAMP := $(BUILDDIR)/generated_config.stamp

# Generated file lists - populated by the Java tools into these .mk fragments.
LIVE_DATA_GENERATED_FILES :=
CONFIG_GENERATED_FILES :=
-include $(PROJECT_DIR)/.dep/generated_live_data_outputs.mk
-include $(PROJECT_DIR)/.dep/generated_config_outputs.mk
# The manifests use paths relative to the firmware directory, including in bootloader builds.
LIVE_DATA_GENERATED_FILES := $(addprefix $(PROJECT_DIR)/,$(LIVE_DATA_GENERATED_FILES))
CONFIG_GENERATED_FILES := $(addprefix $(PROJECT_DIR)/,$(CONFIG_GENERATED_FILES))
GENERATED_FILES := $(LIVE_DATA_GENERATED_FILES) $(CONFIG_GENERATED_FILES)

# A stamp alone cannot prove that outputs survived a clean in another build directory.
ifneq ($(words $(wildcard $(PROJECT_DIR)/.dep/generated_live_data_outputs.mk $(LIVE_DATA_GENERATED_FILES))),$(words $(PROJECT_DIR)/.dep/generated_live_data_outputs.mk $(LIVE_DATA_GENERATED_FILES)))
.PHONY: FORCE_LIVE_DATA
$(LIVE_DATA_STAMP): FORCE_LIVE_DATA
endif
ifneq ($(words $(wildcard $(PROJECT_DIR)/.dep/generated_config_outputs.mk $(CONFIG_GENERATED_FILES))),$(words $(PROJECT_DIR)/.dep/generated_config_outputs.mk $(CONFIG_GENERATED_FILES)))
.PHONY: FORCE_CONFIG
$(CONFIG_STAMP): FORCE_CONFIG
endif

ENUM_INPUTS := $(addprefix $(PROJECT_DIR)/,gen_enum_to_string.sh integration/fome_config.txt \
 controllers/algo/rusefi_hw_enums.h controllers/algo/rusefi_enums.h \
 controllers/algo/engine_types.h controllers/sensors/sensor_type.h controllers/trigger/decoders/sync_edge.h)
ENUM_HEADERS := $(addprefix $(PROJECT_DIR)/,controllers/algo/auto_generated_enums.h \
 controllers/algo/auto_generated_commonenum.h controllers/algo/auto_generated_enginetypes.h \
 controllers/sensors/auto_generated_sensor.h controllers/trigger/decoders/auto_generated_sync_edge.h)
# Git can replace these tracked headers after generation; regenerate when newer or missing.
$(ENUM_HEADERS): ;

JAVA_TOOLS = $(BUILDDIR)/java_tools.cookie

$(JAVA_TOOLS) :
	cd $(PROJECT_DIR)/../java_tools && ./build_tools.sh
	echo "done" > $@

# Stage 1: live data. Extra prerequisites (LiveData.yaml, module *.txt) come from
# .dep/fome_live_data.d, which is auto-included by the build rules.
$(LIVE_DATA_STAMP) : $(JAVA_TOOLS) $(PROJECT_DIR)/integration/LiveData.yaml $(ENUM_INPUTS) $(ENUM_HEADERS)
	@echo Generating live data...
	cd $(PROJECT_DIR) && ./gen_live_documentation.sh $(LIVE_DATA_STAMP)
	@touch $@

# Stage 2: persistent config + TunerStudio ini. Depends on the live-data stamp because it
# consumes stage 1 outputs. Extra prerequisites come from .dep/fome_generated.d.
$(CONFIG_STAMP) : $(JAVA_TOOLS) $(LIVE_DATA_STAMP) $(PROJECT_DIR)/integration/fome_config.txt
	@echo Generating config files...
	cd $(PROJECT_DIR) && ./gen_config_board.sh $(realpath $(BOARD_DIR)) $(SHORT_BOARD_NAME) $(CONFIG_STAMP)
	@touch $@

# Associate each generated file with the stamp recipe that produces it ("multiple outputs
# from one recipe" idiom). These rules carry no recipe, so they never bump a file's mtime -
# a generated file only looks newer to its consumers when its content actually changed.
$(LIVE_DATA_GENERATED_FILES) : $(LIVE_DATA_STAMP)
$(CONFIG_GENERATED_FILES) : $(CONFIG_STAMP)

# Order-only dependency: generation must complete before the first compile, but a stamp
# being newer must NOT force every object to rebuild. The compiler-emitted .dep/*.o.d files
# carry the real generated-header dependencies, so after the first build only the translation
# units whose generated inputs actually changed get recompiled.
$(OBJS) : | $(CONFIG_STAMP)
$(PCHOBJ) : | $(CONFIG_STAMP)

CLEAN_GENERATED_HOOK:
	rm -f $(GENERATED_DIR)/*
	rm -f $(LIVE_DATA_STAMP) $(CONFIG_STAMP)
