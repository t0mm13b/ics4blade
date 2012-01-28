#
# Copyright (C) 2009 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

ifneq ($(BUILD_WITHOUT_PV),true)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

# Set up the OpenCore variables.
include external/opencore/Config.mk
LOCAL_C_INCLUDES := $(PV_INCLUDES)

LOCAL_SRC_FILES := \
    android_surface_output_msm72xx.cpp

LOCAL_CFLAGS := $(PV_CFLAGS_MINUS_VISIBILITY)

LOCAL_SHARED_LIBRARIES := \
    libutils \
    libbinder \
    libcutils \
    libui \
    libhardware\
    libandroid_runtime \
    libmedia \
    libskia \
    libopencore_common \
    libicuuc \
    libopencore_player \
    libsurfaceflinger_client

LOCAL_MODULE := libopencorehw

LOCAL_MODULE_TAGS := optional

LOCAL_LDLIBS += 

include $(BUILD_SHARED_LIBRARY)

endif
