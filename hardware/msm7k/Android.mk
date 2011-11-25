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

#common_msm_dirs := libcopybit liblights libopencorehw librpc libstagefrighthw
#msm7k_dirs := $(common_msm_dirs) boot libgralloc libaudio
#qsd8k_dirs := $(common_msm_dirs) libgralloc-qsd8k libaudio-qsd8k dspcrashd
#msm7x30_dirs := liblights libgralloc-qsd8k librpc libaudio-qdsp5v2

#ifeq ($(TARGET_BOARD_PLATFORM),msm7k)
#  include $(call all-named-subdir-makefiles,$(msm7k_dirs))
#else
#  ifeq ($(TARGET_BOARD_PLATFORM),qsd8k)
#    include $(call all-named-subdir-makefiles,$(qsd8k_dirs))
#  else
#    ifeq ($(TARGET_BOARD_PLATFORM),msm7x30)
#      include $(call all-named-subdir-makefiles,$(msm7x30_dirs))
#    endif
#  endif
#endif

LIBAUDIO := libaudio
ifeq ($(TARGET_PROVIDES_LIBAUDIO),true)
    # A 'true' value assumes it is present in the device's dir,
    # so exclude it here entirely to avoid duplicates
    LIBAUDIO := ""
else ifneq ($(TARGET_PROVIDES_LIBAUDIO),)
    # Target provides a full path to its libaudio
    LIBAUDIO := $(TARGET_PROVIDES_LIBAUDIO)
endif

LIBRPC := librpc
ifeq ($(BOARD_USES_QCOM_LIBRPC),true)
    LIBRPC := librpc-qcom
endif

common_msm_dirs := libcopybit liblights libopencorehw $(LIBRPC) libstagefrighthw

ifeq ($(TARGET_BOARD_PLATFORM),qsd8k)
    ### QSD8k
    ifeq ($(LIBAUDIO),libaudio)
        LIBAUDIO := libaudio-qsd8k
    endif
    qsd8k_dirs := $(common_msm_dirs) libgralloc-qsd8k $(LIBAUDIO) dspcrashd
    include $(call all-named-subdir-makefiles,$(qsd8k_dirs))
else ifeq ($(TARGET_BOOTLOADER_BOARD_NAME),adq)
    ### "adq" board (7x25)
    include $(call all-named-subdir-makefiles,$(common_msm_dirs))
else ifeq ($(TARGET_BOARD_PLATFORM),msm7x30)
    ### MSM7x30
    ifeq ($(LIBAUDIO),libaudio)
        ifeq ($(BOARD_PREBUILT_LIBAUDIO),true)
            LIBAUDIO := libaudio-qdsp5v2
        else
            LIBAUDIO := libaudio-msm7x30
        endif
    endif
    msm7x30_dirs := liblights libgralloc-qsd8k $(LIBRPC) $(LIBAUDIO) liboverlay
    include $(call all-named-subdir-makefiles,$(msm7x30_dirs))
else ifeq ($(TARGET_BOARD_PLATFORM_GPU),qcom-adreno200)
    ### MSM7k with Adreno GPU
    msm7k_adreno_dirs := $(common_msm_dirs) boot libgralloc-qsd8k $(LIBAUDIO)
    include $(call all-named-subdir-makefiles,$(msm7k_adreno_dirs))
else ifeq ($(TARGET_BOARD_PLATFORM),msm7k)
    ### Other MSM7k
    msm7k_dirs := $(common_msm_dirs) boot libgralloc $(LIBAUDIO)
    include $(call all-named-subdir-makefiles,$(msm7k_dirs))
endif


