#!/bin/sh

# Copyright (C) 2010 The Android Open Source Project
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

DEVICE=blade

mkdir -p ../../../vendor/zte/$DEVICE/proprietary
unzip -j -o ../../../${DEVICE}_update.zip system/bin/qmuxd -d ../../../vendor/zte/$DEVICE/proprietary/
chmod 755 ../../../vendor/zte/$DEVICE/proprietary/qmuxd
unzip -j -o ../../../${DEVICE}_update.zip system/bin/akmd2 -d ../../../vendor/zte/$DEVICE/proprietary/
chmod 755 ../../../vendor/zte/$DEVICE/proprietary/akmd2
unzip -j -o ../../../${DEVICE}_update.zip system/bin/hci_qcomm_init -d ../../../vendor/zte/$DEVICE/proprietary/
chmod 755 ../../../vendor/zte/$DEVICE/proprietary/hci_qcomm_init

unzip -j -o ../../../${DEVICE}_update.zip system/etc/init.qcom.bt.sh -d ../../../vendor/zte/$DEVICE/proprietary/

# EGL
unzip -j -o ../../../${DEVICE}_update.zip system/lib/egl/libEGL_adreno200.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/egl/libGLESv2_adreno200.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/egl/libGLESv1_CM_adreno200.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/egl/libq3dtools_adreno200.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libgsl.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/etc/firmware/yamato_pfp.fw -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/etc/firmware/yamato_pm4.fw -d ../../../vendor/zte/$DEVICE/proprietary/

#RIL files
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libril-qc-1.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libril-qcril-hook-oem.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libdiag.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/liboncrpc.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libqmi.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libdsm.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libqueue.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libdll.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libcm.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libmmgsdilib.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libgsdi_exp.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libgstk_exp.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libwms.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libnv.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libwmsts.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libpbmlib.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libdss.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libauth.so -d ../../../vendor/zte/$DEVICE/proprietary/

#camera
unzip -j -o ../../../${DEVICE}_update.zip system/lib/liboemcamera.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libmmjpeg.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libmmipl.so -d ../../../vendor/zte/$DEVICE/proprietary/

#OMX
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxEvrcEnc.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxAacDec.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxWmvDec.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxQcelpDec.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxAmrEnc.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxAdpcmDec.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxEvrcDec.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxH264Dec.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxAmrDec.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxAmrwbDec.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxWmaDec.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxQcelp13Enc.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxMp3Dec.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxMpeg4Dec.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxVidEnc.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxAmrRtpDec.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libOmxAacEnc.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libmm-adspsvc.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libomx_aacdec_sharedlibrary.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libomx_amrdec_sharedlibrary.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libomx_amrenc_sharedlibrary.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libomx_avcdec_sharedlibrary.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libomx_m4vdec_sharedlibrary.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libomx_mp3dec_sharedlibrary.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libomx_sharedlibrary.so -d ../../../vendor/zte/$DEVICE/proprietary/

#GPS
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libloc.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libloc-rpc.so -d ../../../vendor/zte/$DEVICE/proprietary/
unzip -j -o ../../../${DEVICE}_update.zip system/lib/libcommondefs.so -d ../../../vendor/zte/$DEVICE/proprietary/

(cat << EOF) | sed s/__DEVICE__/$DEVICE/g > ../../../vendor/zte/$DEVICE/$DEVICE-vendor-blobs.mk
# Copyright (C) 2010 The Android Open Source Project
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

# This file is generated by device/zte/__DEVICE__/unzip-files.sh - DO NOT EDIT

# All the blobs necessary for blade
PRODUCT_COPY_FILES += \\
    vendor/zte/__DEVICE__/proprietary/libEGL_adreno200.so:system/lib/egl/libEGL_adreno200.so \\
    vendor/zte/__DEVICE__/proprietary/libGLESv2_adreno200.so:system/lib/egl/libGLESv2_adreno200.so \\
    vendor/zte/__DEVICE__/proprietary/libGLESv1_CM_adreno200.so:system/lib/egl/libGLESv1_CM_adreno200.so \\
    vendor/zte/__DEVICE__/proprietary/libq3dtools_adreno200.so:system/lib/egl/libq3dtools_adreno200.so \\
    vendor/zte/__DEVICE__/proprietary/libgsl.so:system/lib//libgsl.so \\
    vendor/zte/__DEVICE__/proprietary/yamato_pfp.fw:system/etc/firmware/yamato_pfp.fw \\
    vendor/zte/__DEVICE__/proprietary/yamato_pm4.fw:system/etc/firmware/yamato_pm4.fw \\
    vendor/zte/__DEVICE__/proprietary/qmuxd:system/bin/qmuxd \\
    vendor/zte/__DEVICE__/proprietary/akmd2:system/bin/akmd2 \\
    vendor/zte/__DEVICE__/proprietary/libril-qc-1.so:system/lib/libril-qc-1.so \\
    vendor/zte/__DEVICE__/proprietary/libril-qcril-hook-oem.so:system/lib/libril-qcril-hook-oem.so \\
    vendor/zte/__DEVICE__/proprietary/libdiag.so:system/lib/libdiag.so \\
    vendor/zte/__DEVICE__/proprietary/init.qcom.bt.sh:system/etc/init.qcom.bt.sh \\
    vendor/zte/__DEVICE__/proprietary/liboncrpc.so:system/lib/liboncrpc.so \\
    vendor/zte/__DEVICE__/proprietary/libqmi.so:system/lib/libqmi.so \\
    vendor/zte/__DEVICE__/proprietary/libdsm.so:system/lib/libdsm.so \\
    vendor/zte/__DEVICE__/proprietary/libqueue.so:system/lib/libqueue.so \\
    vendor/zte/__DEVICE__/proprietary/libdll.so:system/lib/libdll.so \\
    vendor/zte/__DEVICE__/proprietary/libcm.so:system/lib/libcm.so \\
    vendor/zte/__DEVICE__/proprietary/libmmgsdilib.so:system/lib/libmmgsdilib.so \\
    vendor/zte/__DEVICE__/proprietary/libgsdi_exp.so:system/lib/libgsdi_exp.so \\
    vendor/zte/__DEVICE__/proprietary/libgstk_exp.so:system/lib/libgstk_exp.so \\
    vendor/zte/__DEVICE__/proprietary/libwms.so:system/lib/libwms.so \\
    vendor/zte/__DEVICE__/proprietary/libnv.so:system/lib/libnv.so \\
    vendor/zte/__DEVICE__/proprietary/libwmsts.so:system/lib/libwmsts.so \\
    vendor/zte/__DEVICE__/proprietary/libpbmlib.so:system/lib/libpbmlib.so \\
    vendor/zte/__DEVICE__/proprietary/libdss.so:system/lib/libdss.so \\
    vendor/zte/__DEVICE__/proprietary/libauth.so:system/lib/libauth.so \\
    vendor/zte/__DEVICE__/proprietary/liboemcamera.so:system/lib/liboemcamera.so \\
    vendor/zte/__DEVICE__/proprietary/liboemcamera.so:obj/lib/liboemcamera.so \\
    vendor/zte/__DEVICE__/proprietary/libmmjpeg.so:system/lib/libmmjpeg.so \\
    vendor/zte/__DEVICE__/proprietary/libmmipl.so:system/lib/libmmipl.so \\
    vendor/zte/__DEVICE__/proprietary/hci_qcomm_init:system/bin/hci_qcomm_init\\
    vendor/zte/__DEVICE__/proprietary/libOmxEvrcEnc.so:system/lib/libOmxEvrcEnc.so \\
    vendor/zte/__DEVICE__/proprietary/libOmxAacDec.so:system/lib/libOmxAacDec.so \\
    vendor/zte/__DEVICE__/proprietary/libOmxWmvDec.so:system/lib/libOmxWmvDec.so \\
    vendor/zte/__DEVICE__/proprietary/libOmxQcelpDec.so:system/lib/libOmxQcelpDec.so \\
    vendor/zte/__DEVICE__/proprietary/libOmxAmrEnc.so:system/lib/libOmxAmrEnc.so \\
    vendor/zte/__DEVICE__/proprietary/libOmxAdpcmDec.so:system/lib/libOmxAdpcmDec.so \\
    vendor/zte/__DEVICE__/proprietary/libOmxEvrcDec.so:system/lib/libOmxEvrcDec.so \\
    vendor/zte/__DEVICE__/proprietary/libOmxH264Dec.so:system/lib/libOmxH264Dec.so \\
    vendor/zte/__DEVICE__/proprietary/libOmxAmrDec.so:system/lib/libOmxAmrDec.so \\
    vendor/zte/__DEVICE__/proprietary/libOmxAmrwbDec.so:system/lib/libOmxAmrwbDec.so \\
    vendor/zte/__DEVICE__/proprietary/libOmxWmaDec.so:system/lib/libOmxWmaDec.so \\
    vendor/zte/__DEVICE__/proprietary/libOmxQcelp13Enc.so:system/lib/libOmxQcelp13Enc.so \\
    vendor/zte/__DEVICE__/proprietary/libOmxMp3Dec.so:system/lib/libOmxMp3Dec.so \\
    vendor/zte/__DEVICE__/proprietary/libOmxMpeg4Dec.so:system/lib/libOmxMpeg4Dec.so \\
    vendor/zte/__DEVICE__/proprietary/libOmxVidEnc.so:system/lib/libOmxVidEnc.so \\
    vendor/zte/__DEVICE__/proprietary/libOmxAmrRtpDec.so:system/lib/libOmxAmrRtpDec.so \\
    vendor/zte/__DEVICE__/proprietary/libOmxAacEnc.so:system/lib/libOmxAacEnc.so \\
    vendor/zte/__DEVICE__/proprietary/libmm-adspsvc.so:system/lib/libmm-adspsvc.so \\
    vendor/zte/__DEVICE__/proprietary/libomx_aacdec_sharedlibrary.so:system/lib/libomx_aacdec_sharedlibrary.so \\
    vendor/zte/__DEVICE__/proprietary/libomx_amrdec_sharedlibrary.so:system/lib/libomx_amrdec_sharedlibrary.so \\
    vendor/zte/__DEVICE__/proprietary/libomx_amrenc_sharedlibrary.so:system/lib/libomx_amrenc_sharedlibrary.so \\
    vendor/zte/__DEVICE__/proprietary/libomx_avcdec_sharedlibrary.so:system/lib/libomx_avcdec_sharedlibrary.so \\
    vendor/zte/__DEVICE__/proprietary/libomx_m4vdec_sharedlibrary.so:system/lib/libomx_m4vdec_sharedlibrary.so \\
    vendor/zte/__DEVICE__/proprietary/libomx_mp3dec_sharedlibrary.so:system/lib/libomx_mp3dec_sharedlibrary.so \\
    vendor/zte/__DEVICE__/proprietary/libomx_sharedlibrary.so:system/lib/libomx_sharedlibrary.so \\
    vendor/zte/__DEVICE__/proprietary/libloc.so:system/lib/libloc.so \\
    vendor/zte/__DEVICE__/proprietary/libloc.so:obj/lib/libloc.so \\
    vendor/zte/__DEVICE__/proprietary/libloc-rpc.so:system/lib/libloc-rpc.so \\
    vendor/zte/__DEVICE__/proprietary/libcommondefs.so:system/lib/libcommondefs.so

EOF

./setup-makefiles.sh
