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
# This file is the build configuration for a full Android
# build for sapphire hardware. This cleanly combines a set of
# device-specific aspects (drivers) with a device-agnostic
# product configuration (apps).
#

#Dirty hack any better way???

PLATFORM_VERSION := 4.0.3

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/languages_full.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

$(call inherit-product, device/common/gps/gps_eu_supl.mk)

DEVICE_PACKAGE_OVERLAYS := device/zte/blade/overlay

# Discard inherited values and use our own instead.
PRODUCT_NAME := zte_blade
PRODUCT_DEVICE := blade
PRODUCT_MODEL := ZTE Blade

PRODUCT_PACKAGES += \
    librs_jni \
    Gallery3d \
    SpareParts \
    Development \
    Term \
    gralloc.msm7x27 \
    camera.msm7x27 \
    audio.a2dp.default \
    audio.primary.blade \
    audio_policy.blade \
    gps.blade \
    lights.blade \
    sensors.blade \
    libOmxCore \
    libOmxVidEnc \
    FM \
    abtfilt \
    prox_cal \
    dexpreopt

# proprietary side of the device
$(call inherit-product-if-exists, vendor/zte/blade/blade-vendor.mk)

DISABLE_DEXPREOPT := false

PRODUCT_COPY_FILES += \
	device/zte/blade/prebuilt/system/lib/libcamera.so:obj/lib/libcamera.so \
	device/zte/blade/prebuilt/system/lib/libcamera.so:system/lib/libcamera.so \
    	device/zte/blade/prebuilt/system/usr/keylayout/qwerty.kl:system/usr/keylayout/qwerty.kl \
	device/zte/blade/prebuilt/system/usr/keylayout/blade_keypad.kl:system/usr/keylayout/blade_keypad.kl \
	device/zte/blade/prebuilt/system/usr/idc/synaptics-rmi-touchscreen.idc:system/usr/idc/synaptics-rmi-touchscreen.idc \
	device/zte/blade/prebuilt/system/etc/spn-conf.xml:system/etc/spn-conf.xml

# fstab
PRODUCT_COPY_FILES += \
    device/zte/blade/prebuilt/system/etc/vold.fstab:system/etc/vold.fstab

# Init
PRODUCT_COPY_FILES += \
    device/zte/blade/prebuilt/root/init.blade.rc:root/init.blade.rc \
    device/zte/blade/prebuilt/root/init.blade.usb.rc:root/init.blade.usb.rc \
    device/zte/blade/prebuilt/root/ueventd.blade.rc:root/ueventd.blade.rc


# Audio
PRODUCT_COPY_FILES += \
    device/zte/blade/prebuilt/system/etc/AudioFilter.csv:system/etc/AudioFilter.csv \
    device/zte/blade/prebuilt/system/etc/AutoVolumeControl.txt:system/etc/AutoVolumeControl.txt

# WLAN + BT
PRODUCT_COPY_FILES += \
    device/zte/blade/prebuilt/system/etc/init.bt.sh:system/etc/init.bt.sh \
    device/zte/blade/prebuilt/system/etc/wifi/wpa_supplicant.conf:system/etc/wifi/wpa_supplicant.conf \
    device/zte/blade/prebuilt/system/etc/dhcpd/dhcpcd.conf:system/etc/dhcpcd/dhcpcd.conf \
    device/zte/blade/prebuilt/system/bin/hostapd:system/bin/hostapd \
    device/zte/blade/prebuilt/system/etc/wifi/hostapd.conf:system/etc/wifi/hostapd.conf

# Install the features available on this device.
PRODUCT_COPY_FILES += \
    frameworks/base/data/etc/handheld_core_hardware.xml:system/etc/permissions/handheld_core_hardware.xml \
    frameworks/base/data/etc/android.hardware.camera.autofocus.xml:system/etc/permissions/android.hardware.camera.autofocus.xml \
    frameworks/base/data/etc/android.hardware.telephony.gsm.xml:system/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/base/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
    frameworks/base/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/base/data/etc/android.hardware.sensor.proximity.xml:system/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/base/data/etc/android.hardware.sensor.light.xml:system/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/base/data/etc/android.hardware.touchscreen.multitouch.distinct.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.distinct.xml \
    frameworks/base/data/etc/android.software.sip.voip.xml:system/etc/permissions/android.software.sip.voip.xml

#Kernel Modules
PRODUCT_COPY_FILES += device/zte/blade/prebuilt/system/wifi/ar6000.ko:system/wifi/ar6000.ko 

#WiFi firmware
PRODUCT_COPY_FILES += \
    device/zte/blade/prebuilt/system/wifi/regcode:system/wifi/regcode \
    device/zte/blade/prebuilt/system/wifi/data.patch.hw2_0.bin:system/wifi/data.patch.hw2_0.bin \
    device/zte/blade/prebuilt/system/wifi/athwlan.bin.z77:system/wifi/athwlan.bin.z77 \
    device/zte/blade/prebuilt/system/wifi/athtcmd_ram.bin:system/wifi/athtcmd_ram.bin

#Media profile
PRODUCT_COPY_FILES += \
    device/zte/blade/prebuilt/system/etc/media_profiles.xml:system/etc/media_profiles.xml

PRODUCT_PROPERTY_OVERRIDES := \
    keyguard.no_require_sim=true \
    ro.com.android.dateformat=dd-MM-yyyy \
    ro.ril.hsxpa=1 \
    ro.ril.gprsclass=10 \
    ro.media.dec.jpeg.memcap=10000000

PRODUCT_PROPERTY_OVERRIDES += \
    rild.libpath=/system/lib/libril-qc-1.so \
    rild.libargs=-d /dev/smd0 \
    wifi.interface=wlan0 \
    wifi.supplicant_scan_interval=15 \
    ro.com.android.dataroaming=false

PRODUCT_PROPERTY_OVERRIDES += \
    ro.sf.lcd_density=240 \
    ro.sf.hwrotation=180 \
    persist.sys.use_16bpp_alpha=1

# Blade uses high-density artwork where available
PRODUCT_LOCALES += hdpi

# we have enough storage space to hold precise GC data
PRODUCT_TAGS += dalvik.gc.type-precise

# This should not be needed but on-screen keyboard uses the wrong density without it.
PRODUCT_PROPERTY_OVERRIDES += \
    qemu.sf.lcd_density=240 

PRODUCT_PROPERTY_OVERRIDES += \
    keyguard.no_require_sim=true \
    ro.com.android.dateformat=dd-MM-yyyy \
    ro.ril.hsxpa=2 \
    ro.ril.gprsclass=10 \
    ro.telephony.ril.v3=icccardstatus,datacall,signalstrength,facilitylock \
    ro.build.baseband_version=P729BB01 \
    ro.telephony.default_network=0 \
    ro.telephony.call_ring.multiple=false

PRODUCT_PROPERTY_OVERRIDES += \
    ro.com.google.locationfeatures=1 \
    ro.setupwizard.enable_bypass=1 \
    ro.media.dec.jpeg.memcap=20000000 \
    dalvik.vm.lockprof.threshold=500 \
    dalvik.vm.dexopt-flags=m=y \
    dalvik.vm.heapsize=32m \
    dalvik.vm.execution-mode=int:fast \
    dalvik.vm.dexopt-data-only=1 \
    ro.opengles.version=131072  \
    ro.compcache.default=0 \
    persist.sys.strictmode.disable=true \
    persist.sys.usb.config=mass_storage,adb \
    ro.product.manufacturer=ZTE \
    ro.build.fingerprint=google/yakju/maguro:4.0.1/ITL41D/223971:user/release-keys
