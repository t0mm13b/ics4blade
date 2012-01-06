LOCAL_PATH := $(call my-dir)


# Bionic Branches Switches (CM7/AOSP/ICS)
BIONIC_ICS := true


# Make a static library for regex.
include $(CLEAR_VARS)
LOCAL_SRC_FILES := android/regex/regex.c
LOCAL_C_INCLUDES := android/regex
LOCAL_MODULE := libclearsilverregex
include $(BUILD_STATIC_LIBRARY)


# Execute make clean, make prepare and copy profiles required for normal & static busybox (recovery)

include $(CLEAR_VARS)
KERNEL_MODULES_DIR ?= /system/lib/modules
BUSYBOX_CONFIG := minimal full
$(BUSYBOX_CONFIG):
	@echo GENERATE INCLUDES FOR BUSYBOX $@
	@cd $(LOCAL_PATH) && make clean
	cp $(LOCAL_PATH)/.config-$@ $(LOCAL_PATH)/.config
	cd $(LOCAL_PATH) && make prepare
	@#cp $(LOCAL_PATH)/.config $(LOCAL_PATH)/.config-$@
	@mkdir -p $(LOCAL_PATH)/include-$@
	cp $(LOCAL_PATH)/include/*.h $(LOCAL_PATH)/include-$@/
	@rm $(LOCAL_PATH)/include/usage_compressed.h
	@rm -f $(LOCAL_PATH)/.config-old

busybox_prepare: $(BUSYBOX_CONFIG)
LOCAL_MODULE := busybox_prepare
LOCAL_MODULE_TAGS := eng
include $(BUILD_STATIC_LIBRARY)


include $(CLEAR_VARS)

KERNEL_MODULES_DIR ?= /system/lib/modules

SUBMAKE := make -s -C $(LOCAL_PATH) CC=$(CC)

BUSYBOX_SRC_FILES = $(shell cat $(LOCAL_PATH)/busybox-$(BUSYBOX_CONFIG).sources) \
	libbb/android.c

ifeq ($(TARGET_ARCH),arm)
	BUSYBOX_SRC_FILES += \
	android/libc/arch-arm/syscalls/adjtimex.S \
	android/libc/arch-arm/syscalls/getsid.S \
	android/libc/arch-arm/syscalls/stime.S \
	android/libc/arch-arm/syscalls/swapon.S \
	android/libc/arch-arm/syscalls/swapoff.S \
	android/libc/arch-arm/syscalls/sysinfo.S
endif

ifeq ($(TARGET_ARCH),mips)
	BUSYBOX_SRC_FILES += \
	android/libc/arch-mips/syscalls/adjtimex.S \
	android/libc/arch-mips/syscalls/getsid.S \
	android/libc/arch-mips/syscalls/stime.S \
	android/libc/arch-mips/syscalls/swapon.S \
	android/libc/arch-mips/syscalls/swapoff.S \
	android/libc/arch-mips/syscalls/sysinfo.S
endif

BUSYBOX_C_INCLUDES = \
	$(LOCAL_PATH)/include-$(BUSYBOX_CONFIG) \
	$(LOCAL_PATH)/include $(LOCAL_PATH)/libbb \
	bionic/libc/private \
	bionic/libm/include \
	bionic/libm \
	libc/kernel/common \
	$(LOCAL_PATH)/android/regex

BUSYBOX_CFLAGS = \
	-Werror=implicit \
	-DNDEBUG \
	-DANDROID \
	-include include-$(BUSYBOX_CONFIG)/autoconf.h \
	-D'CONFIG_DEFAULT_MODULES_DIR="$(KERNEL_MODULES_DIR)"' \
	-D'BB_VER="$(strip $(shell $(SUBMAKE) kernelversion)) $(BUSYBOX_SUFFIX)"' -DBB_BT=AUTOCONF_TIMESTAMP

# to handle differences in ICS (ipv6)
ifeq ($(BIONIC_ICS),true)
BUSYBOX_CFLAGS += -DBIONIC_ICS
endif


# Build the static lib for the recovery tool

BUSYBOX_CONFIG:=minimal
BUSYBOX_SUFFIX:=static
LOCAL_SRC_FILES := $(BUSYBOX_SRC_FILES)
LOCAL_C_INCLUDES := $(BUSYBOX_C_INCLUDES)
LOCAL_CFLAGS := -Dmain=busybox_driver $(BUSYBOX_CFLAGS)
LOCAL_CFLAGS += \
  -Dgetusershell=busybox_getusershell \
  -Dsetusershell=busybox_setusershell \
  -Dendusershell=busybox_endusershell \
  -Dttyname_r=busybox_ttyname_r \
  -Dgetmntent=busybox_getmntent \
  -Dgetmntent_r=busybox_getmntent_r \
  -Dgenerate_uuid=busybox_generate_uuid
LOCAL_MODULE := libbusybox
LOCAL_MODULE_TAGS := eng
LOCAL_STATIC_LIBRARIES := libcutils libc libm
$(LOCAL_MODULE): busybox_prepare
include $(BUILD_STATIC_LIBRARY)


# Bionic Busybox /system/xbin

include $(CLEAR_VARS)
BUSYBOX_CONFIG:=full
BUSYBOX_SUFFIX:=bionic
LOCAL_SRC_FILES := $(BUSYBOX_SRC_FILES)
ifeq ($(BIONIC_ICS),true)
LOCAL_SRC_FILES += android/libc/__set_errno.c
endif
LOCAL_C_INCLUDES := $(BUSYBOX_C_INCLUDES)
LOCAL_CFLAGS := $(BUSYBOX_CFLAGS)
LOCAL_MODULE := busybox
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)
LOCAL_SHARED_LIBRARIES := libc libcutils libm
LOCAL_STATIC_LIBRARIES := libclearsilverregex
$(LOCAL_MODULE): busybox_prepare
include $(BUILD_EXECUTABLE)

BUSYBOX_LINKS := $(shell cat $(LOCAL_PATH)/busybox-$(BUSYBOX_CONFIG).links)
# nc is provided by external/netcat
exclude := nc
SYMLINKS := $(addprefix $(TARGET_OUT_OPTIONAL_EXECUTABLES)/,$(filter-out $(exclude),$(notdir $(BUSYBOX_LINKS))))
$(SYMLINKS): BUSYBOX_BINARY := $(LOCAL_MODULE)
$(SYMLINKS): $(LOCAL_INSTALLED_MODULE)
	@echo "Symlink: $@ -> $(BUSYBOX_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(BUSYBOX_BINARY) $@

ALL_DEFAULT_INSTALLED_MODULES += $(SYMLINKS)

# We need this so that the installed files could be picked up based on the
# local module name
ALL_MODULES.$(LOCAL_MODULE).INSTALLED := \
    $(ALL_MODULES.$(LOCAL_MODULE).INSTALLED) $(SYMLINKS)


# Static Busybox

include $(CLEAR_VARS)
BUSYBOX_CONFIG:=full
BUSYBOX_SUFFIX:=static
LOCAL_SRC_FILES := $(BUSYBOX_SRC_FILES)
LOCAL_C_INCLUDES := $(BUSYBOX_C_INCLUDES)
LOCAL_CFLAGS := $(BUSYBOX_CFLAGS)
LOCAL_CFLAGS += \
  -Dgetusershell=busybox_getusershell \
  -Dsetusershell=busybox_setusershell \
  -Dendusershell=busybox_endusershell \
  -Dttyname_r=busybox_ttyname_r \
  -Dgetmntent=busybox_getmntent \
  -Dgetmntent_r=busybox_getmntent_r \
  -Dgenerate_uuid=busybox_generate_uuid
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE := static_busybox
LOCAL_MODULE_TAGS := optional
LOCAL_STATIC_LIBRARIES := libclearsilverregex libc libcutils libm
LOCAL_MODULE_CLASS := UTILITY_EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities
LOCAL_UNSTRIPPED_PATH := $(PRODUCT_OUT)/symbols/utilities
LOCAL_MODULE_STEM := busybox
$(LOCAL_MODULE): busybox_prepare
include $(BUILD_EXECUTABLE)
