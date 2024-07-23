include $(PROJECT_DIR)/controllers/modules/fuel_pump/fuel_pump.mk

# Generate definition for all included modules
DDEFS += -DMODULES_LIST=$(MODULES_LIST)
