AUDIO_HW_ROOT := $(call my-dir)

ifeq ($(TARGET_BOARD_PLATFORM),msm8660)
    include $(AUDIO_HW_ROOT)/msm8660/Android.mk
endif

ifeq ($(call is-chipset-in-board-platform,msm7630),true)
    include $(AUDIO_HW_ROOT)/msm7630/Android.mk
endif

ifeq ($(call is-board-platform,msm7627a),true)
    include $(AUDIO_HW_ROOT)/msm7627a/Android.mk
endif

ifeq ($(call is-board-platform,msm7x27),true)
    include $(AUDIO_HW_ROOT)/msm7627_surf/Android.mk
endif
