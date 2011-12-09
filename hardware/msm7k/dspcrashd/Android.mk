ifneq ($(BUILD_TINY_ANDROID),true)
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= dspcrashd.c
LOCAL_MODULE:= dspcrashd

LOCAL_SHARED_LIBRARIES := libc libcutils

LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)
endif
