# Android makefile for the WLAN Module

# Build/Package options for 8960 target
ifeq ($(call is-board-platform,msm8960),true)
WLAN_CHIPSET := prima
WLAN_SELECT := CONFIG_PRIMA_WLAN=m
endif

# Build/Package options for 8974, 8226 targets
ifeq ($(call is-board-platform-in-list,msm8974 msm8226),true)
WLAN_CHIPSET := pronto
WLAN_SELECT := CONFIG_PRONTO_WLAN=m
endif

# Build/Package only in case of supported target
ifneq ($(WLAN_CHIPSET),)

LOCAL_PATH := $(call my-dir)

# This makefile is only for DLKM
ifneq ($(findstring vendor,$(LOCAL_PATH)),)

# Determine if we are Proprietary or Open Source
ifneq ($(findstring opensource,$(LOCAL_PATH)),)
    WLAN_PROPRIETARY := 0
else
    WLAN_PROPRIETARY := 1
endif

ifeq ($(WLAN_PROPRIETARY),1)
    WLAN_BLD_DIR := vendor/qcom/proprietary/wlan
else
    WLAN_BLD_DIR := vendor/qcom/opensource/wlan
endif

ifeq ($(call is-platform-sdk-version-at-least, 16),true)
       DLKM_DIR := $(TOP)/device/qcom/common/dlkm
else
       DLKM_DIR := build/dlkm
endif

# For the proprietary driver the firmware files are handled here
include $(CLEAR_VARS)
LOCAL_MODULE       := WCNSS_qcom_wlan_nv.bin
LOCAL_MODULE_TAGS  := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH  := $(PRODUCT_OUT)/persist
ifdef WIFI_DRIVER_NV_BASE_FILE
LOCAL_SRC_FILES    := ../../../../../$(WIFI_DRIVER_NV_BASE_FILE)
else
LOCAL_SRC_FILES    := firmware_bin/$(LOCAL_MODULE)
endif
include $(BUILD_PREBUILT)

# calibration obakeM data
ifdef WIFI_DRIVER_CAL_FILE_M
include $(CLEAR_VARS)
LOCAL_MODULE       := WCNSS_qcom_wlan_nv_calibration_m.bin
LOCAL_MODULE_TAGS  := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH  := $(TARGET_OUT_ETC)/firmware/wlan/prima
LOCAL_SRC_FILES    := ../../../../../$(WIFI_DRIVER_CAL_FILE_M)
include $(BUILD_PREBUILT)
endif

include $(CLEAR_VARS)
LOCAL_MODULE       := WCNSS_qcom_wlan_nv_calibration_persist.bin
LOCAL_MODULE_TAGS  := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH  := $(PRODUCT_OUT)/persist
ifdef WIFI_DRIVER_CAL_FILE
LOCAL_SRC_FILES    := ../../../../../$(WIFI_DRIVER_CAL_FILE)
else
LOCAL_SRC_FILES    := firmware_bin/WCNSS_qcom_wlan_nv_calibration.bin
endif
include $(BUILD_PREBUILT)

# calibration data will need to be overlay'd per product
include $(CLEAR_VARS)
LOCAL_MODULE       := WCNSS_qcom_wlan_nv_calibration.bin
LOCAL_MODULE_TAGS  := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH  := $(TARGET_OUT_ETC)/firmware/wlan/prima/cal_files
LOCAL_REQUIRED_MODULES := WCNSS_qcom_wlan_nv_calibration_persist.bin
ifdef WIFI_DRIVER_CAL_FILE
LOCAL_SRC_FILES    := ../../../../../$(WIFI_DRIVER_CAL_FILE)
else
LOCAL_SRC_FILES    := firmware_bin/$(LOCAL_MODULE)
endif
include $(BUILD_PREBUILT)

# regulatory obakeM data
ifdef WIFI_DRIVER_REG_FILE_M
include $(CLEAR_VARS)
LOCAL_MODULE       := WCNSS_qcom_wlan_nv_regulatory_m.bin
LOCAL_MODULE_TAGS  := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH  := $(TARGET_OUT_ETC)/firmware/wlan/prima
LOCAL_SRC_FILES    := ../../../../../$(WIFI_DRIVER_REG_FILE_M)
include $(BUILD_PREBUILT)
endif

include $(CLEAR_VARS)
LOCAL_MODULE       := WCNSS_qcom_wlan_nv_regulatory_persist.bin
LOCAL_MODULE_TAGS  := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH  := $(PRODUCT_OUT)/persist
ifdef WIFI_DRIVER_REG_FILE
LOCAL_SRC_FILES    := ../../../../../$(WIFI_DRIVER_REG_FILE)
else
LOCAL_SRC_FILES    := firmware_bin/WCNSS_qcom_wlan_nv_regulatory.bin
endif
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := WCNSS_qcom_wlan_nv_regulatory.bin
LOCAL_MODULE_TAGS  := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH  := $(TARGET_OUT_ETC)/firmware/wlan/prima/cal_files
LOCAL_REQUIRED_MODULES := WCNSS_qcom_wlan_nv_regulatory_persist.bin
ifdef WIFI_DRIVER_REG_FILE
LOCAL_SRC_FILES    := ../../../../../$(WIFI_DRIVER_REG_FILE)
else
LOCAL_SRC_FILES    := firmware_bin/$(LOCAL_MODULE)
endif
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := WCNSS_cfg.dat
LOCAL_MODULE_TAGS  := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH  := $(TARGET_OUT_ETC)/firmware/wlan/prima
LOCAL_SRC_FILES    := firmware_bin/$(LOCAL_MODULE)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := WCNSS_qcom_cfg.ini
LOCAL_MODULE_TAGS  := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH  := $(TARGET_OUT_ETC)/firmware/wlan/prima
ifdef WLAN_CONFIG
LOCAL_SRC_FILES    := firmware_bin/$(WLAN_CONFIG)
else
LOCAL_SRC_FILES    := firmware_bin/$(LOCAL_MODULE)
endif
include $(BUILD_PREBUILT)

ifdef WIFI_DRIVER_HW_RADIO_SPECIFIC_CAL
_hw_radio_cal_models :=

define find-subdir-subdir-l2-files
$(filter-out $(patsubst %,$(1)/%,$(3)),$(patsubst ./%,%,$(shell cd \
            $(LOCAL_PATH) ; find $(1) -maxdepth 2 -type f -name $(2))))
endef

define find-subdir-subdir-l2-slinks
$(filter-out $(patsubst %,$(1)/%,$(3)),$(patsubst ./%,%,$(shell cd \
            $(LOCAL_PATH) ; find $(1) -maxdepth 2 -type l -name $(2))))
endef

define find-subdir-subdir-slink-target
$(filter-out $(patsubst %,$(1)/%,$(3)),$(patsubst ./%,%,$(shell cd \
            $(LOCAL_PATH) ; find $(1) -maxdepth 1 -type l -printf "%l")))
endef


define _add-hw-radio-cal-slink
include $$(CLEAR_VARS)
$(shell mkdir -p $(TARGET_OUT_ETC)/firmware/wlan/prima/cal_files; \
        ln -sf $(notdir $(call find-subdir-subdir-slink-target, $(1))) \
        $(TARGET_OUT_ETC)/firmware/wlan/prima/cal_files/$(notdir $(1)))
endef

define _add-hw-radio-cal
include $$(CLEAR_VARS)
LOCAL_MODULE :=$(notdir $(1))
_hw_radio_cal_models += $$(LOCAL_MODULE)
LOCAL_SRC_FILES := $1
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $$(TARGET_OUT_ETC)/firmware/wlan/prima/cal_files
include $$(BUILD_PREBUILT)
endef

wifi_hw_radio_cal_list :=
$(foreach _hw_cal, $(call find-subdir-subdir-l2-files, ../../../../../$(WIFI_DRIVER_HW_RADIO_SPECIFIC_CAL), "*.bin"), \
  $(eval $(call _add-hw-radio-cal,$(_hw_cal))))


include $(CLEAR_VARS)
LOCAL_MODULE := wifi_hw_radio_specific_cal
LOCAL_MODULE_TAGS := optional
LOCAL_REQUIRED_MODULES := $(_hw_radio_cal_models)
include $(BUILD_PHONY_PACKAGE)

wifi_hw_radio_cal_symlink_list :=
$(foreach _hw_cal_slink, $(call find-subdir-subdir-l2-slinks, ../../../../../$(WIFI_DRIVER_HW_RADIO_SPECIFIC_CAL), "*.bin"), \
  $(eval $(call _add-hw-radio-cal-slink,$(_hw_cal_slink))))
endif
# Build wlan.ko as either prima_wlan.ko or pronto_wlan.ko
###########################################################

# This is set once per LOCAL_PATH, not per (kernel) module
KBUILD_OPTIONS := WLAN_ROOT=../$(WLAN_BLD_DIR)/prima
# We are actually building wlan.ko here, as per the
# requirement we are specifying <chipset>_wlan.ko as LOCAL_MODULE.
# This means we need to rename the module to <chipset>_wlan.ko
# after wlan.ko is built.
KBUILD_OPTIONS += MODNAME=wlan
KBUILD_OPTIONS += BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)
KBUILD_OPTIONS += $(WLAN_SELECT)

include $(CLEAR_VARS)
LOCAL_MODULE              := $(WLAN_CHIPSET)_wlan.ko
LOCAL_MODULE_KBUILD_NAME  := wlan.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := false
LOCAL_MODULE_PATH         := $(TARGET_OUT)/lib/modules/$(WLAN_CHIPSET)
include $(DLKM_DIR)/AndroidKernelModule.mk
###########################################################

#Create symbolic link
$(shell mkdir -p $(TARGET_OUT)/lib/modules; \
        ln -sf /system/lib/modules/$(WLAN_CHIPSET)/$(WLAN_CHIPSET)_wlan.ko \
               $(TARGET_OUT)/lib/modules/wlan.ko)

ifeq ($(WLAN_PROPRIETARY),1)
$(shell mkdir -p $(TARGET_OUT_ETC)/firmware/wlan/prima; \
        ln -sf /persist/WCNSS_qcom_wlan_nv.bin \
        $(TARGET_OUT_ETC)/firmware/wlan/prima/WCNSS_qcom_wlan_nv.bin; \
        ln -sf /data/misc/wifi/WCNSS_qcom_cfg.ini \
        $(TARGET_OUT_ETC)/firmware/wlan/prima/WCNSS_qcom_cfg.ini)

else
$(shell mkdir -p $(TARGET_OUT_ETC)/firmware/wlan/prima; \
        ln -sf /persist/WCNSS_qcom_wlan_nv.bin \
        $(TARGET_OUT_ETC)/firmware/wlan/prima/WCNSS_qcom_wlan_nv.bin; \
        ln -sf /persist/WCNSS_qcom_wlan_nv_calibration_persist.bin \
        $(TARGET_OUT_ETC)/firmware/wlan/prima/WCNSS_qcom_wlan_nv_calibration.bin; \
        ln -sf /persist/WCNSS_qcom_wlan_nv_regulatory_persist.bin \
        $(TARGET_OUT_ETC)/firmware/wlan/prima/WCNSS_qcom_wlan_nv_regulatory.bin)

endif

endif # DLKM check

endif # supported target check
