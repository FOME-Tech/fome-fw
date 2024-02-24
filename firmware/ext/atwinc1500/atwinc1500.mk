ATWINC_DIR = $(PROJECT_DIR)/ext/atwinc1500

ALLINC += \
	$(ATWINC_DIR)


# $(ATWINC_DIR)/bsp/include \
# $(ATWINC_DIR)/bus_wrapper/include \
# $(ATWINC_DIR)/common/include \
# $(ATWINC_DIR)/driver/include \
# $(ATWINC_DIR)/common/include \

ALLCSRC += \
	$(ATWINC_DIR)/common/source/nm_common.c \
	$(ATWINC_DIR)/driver/source/m2m_ate_mode.c \
	$(ATWINC_DIR)/driver/source/m2m_crypto.c \
	$(ATWINC_DIR)/driver/source/m2m_hif.c \
	$(ATWINC_DIR)/driver/source/m2m_hif_crt.c \
	$(ATWINC_DIR)/driver/source/m2m_periph.c \
	$(ATWINC_DIR)/driver/source/m2m_wifi.c \
	$(ATWINC_DIR)/driver/source/nmasic.c \
	$(ATWINC_DIR)/driver/source/nmasic_crt.c \
	$(ATWINC_DIR)/driver/source/nmbus.c \
	$(ATWINC_DIR)/driver/source/nmbus_crt.c \
	$(ATWINC_DIR)/driver/source/nmdrv.c \
	$(ATWINC_DIR)/driver/source/nmdrv_crt.c \
	$(ATWINC_DIR)/driver/source/nmi2c.c \
	$(ATWINC_DIR)/driver/source/nmspi.c \
	$(ATWINC_DIR)/driver/source/nmuart.c \


#$(ATWINC_DIR)/driver/source/m2m_ota.c
#$(ATWINC_DIR)/driver/source/m2m_ssl.c
