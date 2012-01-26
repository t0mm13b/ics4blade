##
##
## Copyright 2009, The Android Open Source Project
##
## Licensed under the Apache License, Version 2.0 (the "License");
## you may not use this file except in compliance with the License.
## You may obtain a copy of the License at
##
##     http://www.apache.org/licenses/LICENSE-2.0
##
## Unless required by applicable law or agreed to in writing, software
## distributed under the License is distributed on an "AS IS" BASIS,
## WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
## See the License for the specific language governing permissions and
## limitations under the License.
##

BASE_PATH := $(call my-dir)
include $(CLEAR_VARS)

# Two ways to control which JS engine is used:
# 1. use JS_ENGINE environment variable, value can be either 'jsc' or 'v8'
#    This is the preferred way.
# 2. if JS_ENGINE is not set, or is not 'jsc' or 'v8', this makefile picks
#    up a default engine to build.
#    To help setup buildbot, a new environment variable, USE_ALT_JS_ENGINE,
#    can be set to true, so that two builds can be different but without
#    specifying which JS engine to use.

# Build libv8 and v8shell
ifeq ($(TARGET_ARCH),arm)
    ENABLE_V8_SNAPSHOT = true
    include $(BASE_PATH)/Android.mksnapshot.mk
    include $(BASE_PATH)/Android.libv8.mk
    include $(BASE_PATH)/Android.v8shell.mk
endif
