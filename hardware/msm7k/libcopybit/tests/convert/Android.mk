LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := test-yv12convert
LOCAL_MODULE_TAGS := eng
ifeq ($(ARCH_ARM_HAVE_NEON),true)
    LOCAL_CFLAGS += -D__ARM_HAVE_NEON
endif

ifeq "$(findstring msm7627a,$(TARGET_PRODUCT))" "msm7627a"
    LOCAL_CFLAGS += -DTARGET_7x27A
endif

LOCAL_C_INCLUDES = hardware/msm7k/libcopybit \
		   hardware/msm7k/libgralloc-qsd8k
LOCAL_SRC_FILES := test-yv12convert.cpp \
                   ../../software_converter.cpp
LOCAL_SHARED_LIBRARIES := liblog libhardware libui libutils libcutils
LOCAL_MODULE_PATH := $(TARGET_OUT_DATA)/copybit_tests
include $(BUILD_EXECUTABLE)
