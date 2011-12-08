/*
** Copyright 2008, The Android Open-Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
** You may obtain a copy of the License at 
**
**     http://www.apache.org/licenses/LICENSE-2.0 
**
** Unless required by applicable law or agreed to in writing, software 
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/

#ifndef ANDROID_HARDWARE_QUALCOMM_CAMERA_HARDWARE_H
#define ANDROID_HARDWARE_QUALCOMM_CAMERA_HARDWARE_H

#include <camera/CameraHardwareInterface.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>

extern "C" {
    #include <linux/android_pmem.h>
}

namespace android {

class QualcommCameraHardware : public CameraHardwareInterface {
public:

    virtual sp<IMemoryHeap> getPreviewHeap() const;
    virtual sp<IMemoryHeap> getRawHeap() const;

    virtual status_t    dump(int fd, const Vector<String16>& args) const;
    virtual status_t    startPreview(preview_callback cb, void* user);
    virtual void        stopPreview();
    virtual bool        previewEnabled();
    virtual status_t    startRecording(recording_callback cb, void* user);
    virtual void        stopRecording();
    virtual bool        recordingEnabled();
    virtual void        releaseRecordingFrame(const sp<IMemory>& mem);
    virtual status_t    autoFocus(autofocus_callback, void *user);
    virtual status_t    takePicture(shutter_callback,
                                    raw_callback,
                                    jpeg_callback,
                                    void* user);
    virtual status_t    cancelPicture(bool cancel_shutter,
                                      bool cancel_raw, bool cancel_jpeg);
    virtual status_t    setParameters(const CameraParameters& params);
    virtual CameraParameters  getParameters() const;

    virtual void release();

    static sp<CameraHardwareInterface> createInstance();
    static sp<QualcommCameraHardware> getInstance();

    void* get_preview_mem(uint32_t size, uint32_t *phy_addr, uint32_t index);
    void* get_raw_mem(uint32_t size, uint32_t *phy_addr, uint32_t index);
    void free_preview_mem(uint32_t *phy_addr, uint32_t size, uint32_t index);
    void free_raw_mem(uint32_t *phy_addr, uint32_t size, uint32_t index);

private:

    QualcommCameraHardware();
    virtual ~QualcommCameraHardware();
    status_t startPreviewInternal(preview_callback pcb, void *puser,
                                  recording_callback rcb, void *ruser);
    void stopPreviewInternal();

    static wp<QualcommCameraHardware> singleton;

    /* These constants reflect the number of buffers that libqcamera requires
       for preview and raw, and need to be updated when libqcamera
       changes.
    */
    static const int kPreviewBufferCount = 4;
    static const int kRawBufferCount = 1;
    static const int kJpegBufferCount = 1;
    static const int kRawFrameHeaderSize = 0x48;

    //TODO: put the picture dimensions in the CameraParameters object;
    CameraParameters mParameters;
    int mPreviewHeight;
    int mPreviewWidth;
    int mRawHeight;
    int mRawWidth;

    void receivePreviewFrame(camera_frame_type *frame);

    static void stop_camera_cb(camera_cb_type cb,
            const void *client_data,
            camera_func_type func,
            int32_t parm4);

    static void camera_cb(camera_cb_type cb,
            const void *client_data,
            camera_func_type func,
            int32_t parm4);

    // This class represents a heap which maintains several contiguous
    // buffers.  The heap may be backed by pmem (when pmem_pool contains
    // the name of a /dev/pmem* file), or by ashmem (when pmem_pool == NULL).

    struct MemPool : public RefBase {
        MemPool(int buffer_size, int num_buffers,
                int frame_size,
                int frame_offset,
                const char *name);

        virtual ~MemPool() = 0;

        void completeInitialization();
        bool initialized() const { 
            return mHeap != NULL && mHeap->base() != MAP_FAILED;
        }

        virtual status_t dump(int fd, const Vector<String16>& args) const;

        int mBufferSize;
        int mNumBuffers;
        int mFrameSize;
        int mFrameOffset;
        sp<MemoryHeapBase> mHeap;
        sp<MemoryBase> *mBuffers;

        const char *mName;
    };

    struct AshmemPool : public MemPool {
        AshmemPool(int buffer_size, int num_buffers,
                   int frame_size,
                   int frame_offset,
                   const char *name);
    };

    struct PmemPool : public MemPool {
        PmemPool(const char *pmem_pool,
                int buffer_size, int num_buffers,
                int frame_size,
                int frame_offset,
                const char *name);
        virtual ~PmemPool() { }
        int mFd;
        uint32_t mAlignedSize;
        struct pmem_region mSize;
    };

    struct PreviewPmemPool : public PmemPool {
        virtual ~PreviewPmemPool();
        PreviewPmemPool(int buffer_size, int num_buffers,
                        int frame_size,
                        int frame_offset,
                        const char *name);
    };

    struct RawPmemPool : public PmemPool {
        virtual ~RawPmemPool();
        RawPmemPool(const char *pmem_pool,
                    int buffer_size, int num_buffers,
                    int frame_size,
                    int frame_offset,
                    const char *name);
    };

    sp<PreviewPmemPool> mPreviewHeap;
    sp<RawPmemPool> mRawHeap;
    sp<AshmemPool> mJpegHeap;

    void startCameraIfNecessary();
    bool initPreview();
    void deinitPreview();
    bool initRaw(bool initJpegHeap);

    void initDefaultParameters();
    void initCameraParameters();
    void setCameraDimensions();

    // The states described by qualcomm_camera_state are very similar to the
    // CAMERA_FUNC_xxx notifications reported by libqcamera.  The differences
    // are that they reflect not only the response from libqcamera, but also
    // the requests made by the clients of this object.  For example,
    // QCS_PREVIEW_REQUESTED is a state that we enter when we call
    // QualcommCameraHardware::startPreview(), and stay in until libqcamera
    // confirms that it has received the start-preview command (but not
    // actually initiated preview yet).
    //
    // NOTE: keep those values small; they are used internally as indices
    //       into a array of strings.
    // NOTE: if you add to this enumeration, make sure you update
    //       getCameraStateStr().

    enum qualcomm_camera_state {
        QCS_INIT,
        QCS_IDLE,
        QCS_ERROR,
        QCS_PREVIEW_IN_PROGRESS,
        QCS_WAITING_RAW,
        QCS_WAITING_JPEG,
        /* internal states */
        QCS_INTERNAL_PREVIEW_STOPPING,
        QCS_INTERNAL_PREVIEW_REQUESTED,
        QCS_INTERNAL_RAW_REQUESTED,
        QCS_INTERNAL_STOPPING,
    };

    volatile qualcomm_camera_state mCameraState;
    static const char* const getCameraStateStr(qualcomm_camera_state s);
    qualcomm_camera_state change_state(qualcomm_camera_state new_state,
                                       bool lock = true);

    void notifyShutter();
    void receiveJpegPictureFragment(JPEGENC_CBrtnType *encInfo);

    void receivePostLpmRawPicture(camera_frame_type *frame);
    void receiveRawPicture(camera_frame_type *frame);
    void receiveJpegPicture(void);

    Mutex mLock; // API lock -- all public methods
    Mutex mCallbackLock;
    Mutex mStateLock;
    Condition mStateWait;

    /* mJpegSize keeps track of the size of the accumulated JPEG.  We clear it
       when we are about to take a picture, so at any time it contains either
       zero, or the size of the last JPEG picture taken.
    */
    uint32_t mJpegSize;
    camera_handle_type camera_handle;
    camera_encode_properties_type encode_properties;
    camera_position_type pt;

    shutter_callback    mShutterCallback;
    raw_callback        mRawPictureCallback;
    jpeg_callback       mJpegPictureCallback;
    void                *mPictureCallbackCookie;

    autofocus_callback  mAutoFocusCallback;
    void                *mAutoFocusCallbackCookie;

    preview_callback    mPreviewCallback;
    void                *mPreviewCallbackCookie;
    recording_callback  mRecordingCallback;
    void                *mRecordingCallbackCookie;
    bool setCallbacks(preview_callback pcb, void *pu,
                      recording_callback rcb, void *ru);

    int                 mPreviewFrameSize;
    int                 mRawSize;
    int                 mJpegMaxSize;

    // hack to prevent black frame on first preview
    int                 mPreviewCount;

#if DLOPEN_LIBQCAMERA == 1
    void *libqcamera;
#endif
};

}; // namespace android

#endif


