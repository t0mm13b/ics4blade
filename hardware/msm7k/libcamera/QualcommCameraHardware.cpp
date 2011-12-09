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

// TODO
// -- replace Condition::wait with Condition::waitRelative
// -- use read/write locks

#define LOG_NDEBUG 0
#define LOG_TAG "QualcommCameraHardware"
#include <utils/Log.h>
#include <utils/threads.h>
#include <binder/MemoryHeapPmem.h>
#include <utils/String16.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#if HAVE_ANDROID_OS
#include <linux/android_pmem.h>
#endif
#include <camera_ifc.h>
#if DLOPEN_LIBQCAMERA
#include <dlfcn.h>
#endif

#define PRINT_TIME 0

extern "C" {

static inline void print_time()
{
#if PRINT_TIME
    struct timeval time; 
    gettimeofday(&time, NULL);
    LOGV("time: %lld us.", time.tv_sec * 1000000LL + time.tv_usec);
#endif
}

typedef struct {
    int width;
    int height;
} preview_size_type;

// These sizes have to be a multiple of 16 in each dimension
static preview_size_type preview_sizes[] = {
    { 480, 320 }, // HVGA
    { 432, 320 }, // 1.35-to-1, for photos. (Rounded up from 1.3333 to 1)
    { 352, 288 }, // CIF
    { 320, 240 }, // QVGA
    { 240, 160 }, // SQVGA
    { 176, 144 }, // QCIF
};
#define PREVIEW_SIZE_COUNT (sizeof(preview_sizes)/sizeof(preview_size_type))

// default preview size is QVGA
#define DEFAULT_PREVIEW_SETTING 0

#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

    /* some functions we need from libqcamera */
    extern void rex_start();
    extern void rex_shutdown();

    /* callbacks */
#if DLOPEN_LIBQCAMERA == 0
    extern void (*rex_signal_ready)();
    extern uint8_t* (*cam_mmap_preview)(uint32_t size,
                                                 uint32_t *phy_addr,
                                                 uint32_t index);
    extern uint8_t* (*cam_mmap_snapshot)(uint32_t size,
                                                 uint32_t *phy_addr,
                                                 uint32_t index);
    extern int (*cam_munmap_preview)(uint32_t *phy_addr,
                                     uint32_t size,
                                     uint32_t index);
    extern int (*cam_munmap_snapshot)(uint32_t *phy_addr,
                                      uint32_t size,
                                      uint32_t index);

    extern clear_module_pmem(qdsp_module_type module);

    extern void camera_assoc_pmem(qdsp_module_type module,
                                  int pmem_fd, 
                                  void *addr,
                                  uint32_t length,
                                  int external);

    extern int camera_release_pmem(qdsp_module_type module,
                                   void *addr,
                                   uint32_t size,
                                   uint32_t force);

#define LINK_camera_assoc_pmem             camera_assoc_pmem
#define LINK_clear_module_pmem             clear_module_pmem
#define LINK_camera_release_pmem           camera_release_pmem
#define LINK_camera_encode_picture         camera_encode_picture
#define LINK_camera_init                   camera_init
#define LINK_camera_af_init                camera_af_init
#define LINK_camera_release_frame          camera_release_frame
#define LINK_camera_set_dimensions         camera_set_dimensions
#define LINK_camera_set_encode_properties  camera_set_encode_properties
#define LINK_camera_set_parm               camera_set_parm
#define LINK_camera_set_parm_2             camera_set_parm_2
#define LINK_camera_set_position           camera_set_position
#define LINK_camera_set_thumbnail_properties camera_set_thumbnail_properties
#define LINK_camera_start                  camera_start
#define LINK_camera_start_preview          camera_start_preview
#define LINK_camera_start_focus            camera_start_focus
#define LINK_camera_stop_focus             camera_stop_focus
#define LINK_camera_stop                   camera_stop
#define LINK_camera_stop_preview           camera_stop_preview
#define LINK_camera_take_picture           camera_take_picture
#define LINK_rex_shutdown                  rex_shutdown
#define LINK_rex_start                     rex_start
#define LINK_rex_signal_ready              rex_signal_ready

#define LINK_cam_mmap_preview   cam_mmap_preview
#define LINK_cam_munmap_preview cam_munmap_preview
#define LINK_cam_mmap_snapshot  cam_mmap_snapshot
#define LINK_cam_munmap_snapshot cam_munmap_snapshot

#else

    /* Function pointers to assign to */

    void (**LINK_rex_signal_ready)();

    uint8_t* (**LINK_cam_mmap_preview)(
        uint32_t size,
        uint32_t *phy_addr,
        uint32_t index);

    int (**LINK_cam_munmap_preview)(
        uint32_t *phy_addr,
        uint32_t size,
        uint32_t index);

    uint8_t* (**LINK_cam_mmap_snapshot)(
        uint32_t size,
        uint32_t *phy_addr,
        uint32_t index);

    int (**LINK_cam_munmap_snapshot)(
        uint32_t *phy_addr,
        uint32_t size,
        uint32_t index);

    /* Function pointers to resolve */

    void (*LINK_camera_assoc_pmem)(qdsp_module_type module,
                                   int pmem_fd, 
                                   void *addr,
                                   uint32_t length,
                                   int external);

    void (*LINK_clear_module_pmem)(qdsp_module_type module);

    int (*LINK_camera_release_pmem)(qdsp_module_type module,
                                    void *addr,
                                    uint32_t size,
                                    uint32_t force);

    camera_ret_code_type (*LINK_camera_encode_picture) (
        camera_frame_type *frame,
        camera_handle_type *handle,
        camera_cb_f_type callback,
        void *client_data);

    void (*LINK_camera_init)(void);

    void (*LINK_camera_af_init)(void);

    camera_ret_code_type (*LINK_camera_release_frame)(void);

    camera_ret_code_type (*LINK_camera_set_dimensions) (
        uint16_t picture_width,
        uint16_t picture_height,
        uint16_t display_width,
#ifdef FEATURE_CAMERA_V7
        uint16_t display_height,
#endif
        camera_cb_f_type callback,
        void *client_data);

    camera_ret_code_type (*LINK_camera_set_encode_properties)(
        camera_encode_properties_type *encode_properties);

    camera_ret_code_type (*LINK_camera_set_parm) (
        camera_parm_type id,
        int32_t          parm,
        camera_cb_f_type callback,
        void            *client_data);

    camera_ret_code_type (*LINK_camera_set_parm_2) (
        camera_parm_type id,
        int32_t          parm1,
        int32_t          parm2,
        camera_cb_f_type callback,
        void            *client_data);

    camera_ret_code_type (*LINK_camera_set_position) (
        camera_position_type *position,
        camera_cb_f_type      callback,
        void                 *client_data);

    camera_ret_code_type (*LINK_camera_set_thumbnail_properties) (
                              uint32_t width,
                              uint32_t height,
                              uint32_t quality);

    camera_ret_code_type (*LINK_camera_start) (
        camera_cb_f_type callback,
        void *client_data
#ifdef FEATURE_NATIVELINUX
        ,int  display_height,
        int  display_width
#endif /* FEATURE_NATIVELINUX */
        );

    camera_ret_code_type (*LINK_camera_start_preview) (
        camera_cb_f_type callback,
        void *client_data);

    camera_ret_code_type (*LINK_camera_start_focus) (
        camera_focus_e_type focus,
        camera_cb_f_type callback,
        void *client_data);

    camera_ret_code_type (*LINK_camera_stop_focus) (void);

    camera_ret_code_type (*LINK_camera_stop) (
        camera_cb_f_type callback,
        void *client_data);

    camera_ret_code_type (*LINK_camera_stop_preview) (void);

    camera_ret_code_type (*LINK_camera_take_picture) (
        camera_cb_f_type    callback,
        void               *client_data
#if !defined FEATURE_CAMERA_ENCODE_PROPERTIES && defined FEATURE_CAMERA_V7
        ,camera_raw_type camera_raw_mode
#endif /* nFEATURE_CAMERA_ENCODE_PROPERTIES && FEATURE_CAMERA_V7 */
        );
    
    int (*LINK_rex_start)(void);

    int (*LINK_rex_shutdown)(void);

#endif

}

#include "QualcommCameraHardware.h"

namespace android {

    static Mutex singleton_lock;
    static Mutex rex_init_lock;
    static Condition rex_init_wait;

    static uint8_t* malloc_preview(uint32_t, uint32_t *, uint32_t);
    static uint8_t* malloc_raw(uint32_t, uint32_t *, uint32_t);
    static int free_preview(uint32_t *, uint32_t , uint32_t);
    static int free_raw(uint32_t *, uint32_t , uint32_t);
    static int reassoc(qdsp_module_type module);
    static void cb_rex_signal_ready(void);

    QualcommCameraHardware::QualcommCameraHardware()
        : mParameters(),
          mPreviewHeight(-1),
          mPreviewWidth(-1),
          mRawHeight(-1),
          mRawWidth(-1),
          mCameraState(QCS_INIT),
          mShutterCallback(0),
          mRawPictureCallback(0),
          mJpegPictureCallback(0),
          mPictureCallbackCookie(0),
          mAutoFocusCallback(0),
          mAutoFocusCallbackCookie(0),
          mPreviewCallback(0),
          mPreviewCallbackCookie(0),
          mRecordingCallback(0),
          mRecordingCallbackCookie(0),
          mPreviewFrameSize(0),
          mRawSize(0),
          mPreviewCount(0)
    {
        LOGV("constructor EX");
    }

    void QualcommCameraHardware::initDefaultParameters()
    {
        CameraParameters p;

        preview_size_type* ps = &preview_sizes[DEFAULT_PREVIEW_SETTING];
        p.setPreviewSize(ps->width, ps->height);
        p.setPreviewFrameRate(15);
        p.setPreviewFormat("yuv420sp");
        p.setPictureFormat("jpeg"); // we do not look at this currently
        p.setPictureSize(2048, 1536);
        p.set("jpeg-quality", "100"); // maximum quality

        // These values must be multiples of 16, so we can't do 427x320, which is the exact size on
        // screen we want to display at. 480x360 doesn't work either since it's a multiple of 8.
        p.set("jpeg-thumbnail-width", "512");
        p.set("jpeg-thumbnail-height", "384");
        p.set("jpeg-thumbnail-quality", "90");

        p.set("nightshot-mode", "0"); // off
        p.set("luma-adaptation", "0"); // FIXME: turning it on causes a crash
        p.set("antibanding", "auto"); // flicker detection and elimination
        p.set("whitebalance", "auto");
        p.set("rotation", "0");

#if 0
        p.set("gps-timestamp", "1199145600"); // Jan 1, 2008, 00:00:00
        p.set("gps-latitude", "37.736071"); // A little house in San Francisco
        p.set("gps-longitude", "-122.441983");
        p.set("gps-altitude", "21"); // meters
#endif

        // List supported picture size values
        p.set("picture-size-values", "2048x1536,1600x1200,1024x768");

        // List supported antibanding values
        p.set("antibanding-values",
              "off,50hz,60hz,auto");

        // List supported effects:
        p.set("effect-values",
              "off,mono,negative,solarize,sepia,posterize,whiteboard,"\
              "blackboard,aqua");

        // List supported exposure-offset:
        p.set("exposure-offset-values",
              "0,1,2,3,4,5,6,7,8,9,10");

        // List of whitebalance values
        p.set("whitebalance-values",
              "auto,incandescent,fluorescent,daylight,cloudy");

        // List of ISO values
        p.set("iso-values", "auto,high");

        if (setParameters(p) != NO_ERROR) {
            LOGE("Failed to set default parameters?!");
        }
    }

#define ROUND_TO_PAGE(x)  (((x)+0xfff)&~0xfff)

    // Called with mStateLock held!
    void QualcommCameraHardware::startCameraIfNecessary()
    {
        if (mCameraState == QCS_INIT) {

#if DLOPEN_LIBQCAMERA == 1

            LOGV("loading libqcamera");
            libqcamera = ::dlopen("liboemcamera.so", RTLD_NOW);
            if (!libqcamera) {
                LOGE("FATAL ERROR: could not dlopen liboemcamera.so: %s", dlerror());
                return;
            }
  
            *(void **)&LINK_camera_assoc_pmem =
                ::dlsym(libqcamera, "camera_assoc_pmem");
            *(void **)&LINK_clear_module_pmem =
                ::dlsym(libqcamera, "clear_module_pmem");
            *(void **)&LINK_camera_release_pmem =
                ::dlsym(libqcamera, "camera_release_pmem");
            *(void **)&LINK_camera_encode_picture =
                ::dlsym(libqcamera, "camera_encode_picture");
            *(void **)&LINK_camera_init =
                ::dlsym(libqcamera, "camera_init");
            *(void **)&LINK_camera_af_init =
                ::dlsym(libqcamera, "camera_af_init");
            *(void **)&LINK_camera_release_frame =
                ::dlsym(libqcamera, "camera_release_frame");
            *(void **)&LINK_camera_set_dimensions =
                ::dlsym(libqcamera, "camera_set_dimensions");
            *(void **)&LINK_camera_set_encode_properties =
                ::dlsym(libqcamera, "camera_set_encode_properties");
            *(void **)&LINK_camera_set_parm =
                ::dlsym(libqcamera, "camera_set_parm");
            *(void **)&LINK_camera_set_parm_2 =
                ::dlsym(libqcamera, "camera_set_parm_2");
            *(void **)&LINK_camera_set_position =
                ::dlsym(libqcamera, "camera_set_position");
            *(void **)&LINK_camera_set_thumbnail_properties  =
                ::dlsym(libqcamera, "camera_set_thumbnail_properties");
            *(void **)&LINK_camera_start =
                ::dlsym(libqcamera, "camera_start");
            *(void **)&LINK_camera_start_preview =
                ::dlsym(libqcamera, "camera_start_preview");
            *(void **)&LINK_camera_start_focus =
                ::dlsym(libqcamera, "camera_start_focus");
            *(void **)&LINK_camera_stop_focus =
                ::dlsym(libqcamera, "camera_stop_focus");
            *(void **)&LINK_camera_stop =
                ::dlsym(libqcamera, "camera_stop");
            *(void **)&LINK_camera_stop_preview =
                ::dlsym(libqcamera, "camera_stop_preview");
            *(void **)&LINK_camera_take_picture =
                ::dlsym(libqcamera, "camera_take_picture");
            *(void **)&LINK_rex_shutdown =
                ::dlsym(libqcamera, "rex_shutdown");
            *(void **)&LINK_rex_start =
                ::dlsym(libqcamera, "rex_start");
            *(void **)&LINK_rex_signal_ready =
                ::dlsym(libqcamera, "rex_signal_ready");
            *(void **)&LINK_cam_mmap_preview =
                ::dlsym(libqcamera, "cam_mmap_preview");
            *(void **)&LINK_cam_munmap_preview =
                ::dlsym(libqcamera, "cam_munmap_preview");
            *(void **)&LINK_cam_mmap_snapshot =
                ::dlsym(libqcamera, "cam_mmap_snapshot");
            *(void **)&LINK_cam_munmap_snapshot =
                ::dlsym(libqcamera, "cam_munmap_snapshot");
            
            *LINK_rex_signal_ready = cb_rex_signal_ready;
            *LINK_cam_mmap_preview = malloc_preview;
            *LINK_cam_munmap_preview = free_preview;
            *LINK_cam_mmap_snapshot = malloc_raw;
            *LINK_cam_munmap_snapshot = free_raw;
#else
            LINK_rex_signal_ready = cb_rex_signal_ready;
            LINK_cam_mmap_preview = malloc_preview;
            LINK_cam_munmap_preview = free_preview;
            LINK_cam_mmap_snapshot = malloc_raw;
            LINK_cam_munmap_snapshot = free_raw;
#endif // DLOPEN_LIBQCAMERA == 1
            
            rex_init_lock.lock();
            LINK_rex_start();
            LOGV("waiting for REX to initialize.");
            rex_init_wait.wait(rex_init_lock);
            LOGV("REX is ready.");
            rex_init_lock.unlock();
            
            LINK_camera_init();
            
            LOGV("starting REX emulation");
            // NOTE: camera_start() takes (height, width), not (width, height).
            LINK_camera_start(camera_cb, this,
                              mPreviewHeight, mPreviewWidth);
            while(mCameraState != QCS_IDLE &&
                  mCameraState != QCS_ERROR) {
                LOGV("init camera: waiting for QCS_IDLE");
                mStateWait.wait(mStateLock);
                LOGV("init camera: woke up");
            }
            LOGV("init camera: initializing parameters");
        }
        else LOGV("camera hardware has been started already");
    }

    status_t QualcommCameraHardware::dump(int fd, const Vector<String16>& args) const
    {
        const size_t SIZE = 256;
        char buffer[SIZE];
        String8 result;
        
        // Dump internal primitives.
        snprintf(buffer, 255, "QualcommCameraHardware::dump: state (%d)\n", mCameraState);
        result.append(buffer);
        snprintf(buffer, 255, "preview width(%d) x height (%d)\n", mPreviewWidth, mPreviewHeight);
        result.append(buffer);
        snprintf(buffer, 255, "raw width(%d) x height (%d)\n", mRawWidth, mRawHeight);
        result.append(buffer);
        snprintf(buffer, 255, "preview frame size(%d), raw size (%d), jpeg size (%d) and jpeg max size (%d)\n", mPreviewFrameSize, mRawSize, mJpegSize, mJpegMaxSize);
        result.append(buffer);
        write(fd, result.string(), result.size());
        
        // Dump internal objects.
        if (mPreviewHeap != 0) {
            mPreviewHeap->dump(fd, args);
        }
        if (mRawHeap != 0) {
            mRawHeap->dump(fd, args);
        }
        if (mJpegHeap != 0) {
            mJpegHeap->dump(fd, args);
        }
        mParameters.dump(fd, args);
        return NO_ERROR;
    }
    
    bool QualcommCameraHardware::initPreview()
    {
//      LINK_clear_module_pmem(QDSP_MODULE_VFETASK);

        startCameraIfNecessary();

        // Tell libqcamera what the preview and raw dimensions are.  We
        // call this method even if the preview dimensions have not changed,
        // because the picture ones may have.
        //
        // NOTE: if this errors out, mCameraState != QCS_IDLE, which will be
        //       checked by the caller of this method.

        setCameraDimensions();

        LOGV("initPreview: preview size=%dx%d", mPreviewWidth, mPreviewHeight);

        mPreviewFrameSize = mPreviewWidth * mPreviewHeight * 3 / 2; // reality
        mPreviewHeap =
            new PreviewPmemPool(kRawFrameHeaderSize +
                                mPreviewWidth * mPreviewHeight * 2, // worst
                                kPreviewBufferCount,
                                mPreviewFrameSize,
                                kRawFrameHeaderSize,
                                "preview");
        
        if (!mPreviewHeap->initialized()) {
            mPreviewHeap = NULL;
            return false;
        }

//      LINK_camera_af_init();

        return true;
    }

    void QualcommCameraHardware::deinitPreview()
    {
        mPreviewHeap = NULL;
    }

    // Called with mStateLock held!
    bool QualcommCameraHardware::initRaw(bool initJpegHeap)
    {
        LOGV("initRaw E");
        startCameraIfNecessary();

        // Tell libqcamera what the preview and raw dimensions are.  We
        // call this method even if the preview dimensions have not changed,
        // because the picture ones may have.
        //
        // NOTE: if this errors out, mCameraState != QCS_IDLE, which will be
        //       checked by the caller of this method.

        setCameraDimensions();

        LOGV("initRaw: picture size=%dx%d",
             mRawWidth, mRawHeight);

        // Note that we enforce yuv420 in setParameters().

        mRawSize =
            mRawWidth * mRawHeight * 3 / 2; /* reality */

        mJpegMaxSize = mRawWidth * mRawHeight * 2;

        LOGV("initRaw: clearing old mJpegHeap.");
        mJpegHeap = NULL;

        LOGV("initRaw: initializing mRawHeap.");
        mRawHeap =
            new RawPmemPool("/dev/pmem_camera",
                            kRawFrameHeaderSize + mJpegMaxSize, /* worst */
                            kRawBufferCount,
                            mRawSize,
                            kRawFrameHeaderSize,
                            "snapshot camera");

        if (!mRawHeap->initialized()) {
            LOGE("initRaw X failed: error initializing mRawHeap");
            mRawHeap = NULL;
            return false;
        }

        if (initJpegHeap) {
            LOGV("initRaw: initializing mJpegHeap.");
            mJpegHeap =
                new AshmemPool(mJpegMaxSize,
                               kJpegBufferCount,
                               0, // we do not know how big the picture wil be
                               0,
                               "jpeg");
            if (!mJpegHeap->initialized()) {
                LOGE("initRaw X failed: error initializing mJpegHeap.");
                mJpegHeap = NULL;
                mRawHeap = NULL;
                return false;
            }
        }

        LOGV("initRaw X success");
        return true;
    }

    void QualcommCameraHardware::release()
    {
        LOGV("release E");

        Mutex::Autolock l(&mLock);

        // Either preview was ongoing, or we are in the middle or taking a
        // picture.  It's the caller's responsibility to make sure the camera
        // is in the idle or init state before destroying this object.

        if (mCameraState != QCS_IDLE && mCameraState != QCS_INIT) {
            LOGE("Serious error: the camera state is %s, "
                 "not QCS_IDLE or QCS_INIT!",
                 getCameraStateStr(mCameraState));
        }
        
        mStateLock.lock();
        if (mCameraState != QCS_INIT) {
            // When libqcamera detects an error, it calls camera_cb from the
            // call to LINK_camera_stop, which would cause a deadlock if we
            // held the mStateLock.  For this reason, we have an intermediate
            // state QCS_INTERNAL_STOPPING, which we use to check to see if the
            // camera_cb was called inline.
            mCameraState = QCS_INTERNAL_STOPPING;
            mStateLock.unlock();

            LOGV("stopping camera.");
            LINK_camera_stop(stop_camera_cb, this);

            mStateLock.lock();
            if (mCameraState == QCS_INTERNAL_STOPPING) {
                while (mCameraState != QCS_INIT &&
                       mCameraState != QCS_ERROR) {
                    LOGV("stopping camera: waiting for QCS_INIT");
                    mStateWait.wait(mStateLock);
                }
            }

            LOGV("Shutting REX down.");
            LINK_rex_shutdown();
            LOGV("REX has shut down.");
#if DLOPEN_LIBQCAMERA
            if (libqcamera) {
                unsigned ref = ::dlclose(libqcamera);
                LOGV("dlclose(libqcamera) refcount %d", ref);
            }
#endif
            mCameraState = QCS_INIT;
        }
        mStateLock.unlock();

        LOGV("release X");
    }

    QualcommCameraHardware::~QualcommCameraHardware()
    {
        LOGV("~QualcommCameraHardware E");
        Mutex::Autolock singletonLock(&singleton_lock);
        singleton.clear();
        LOGV("~QualcommCameraHardware X");
    }

    sp<IMemoryHeap> QualcommCameraHardware::getPreviewHeap() const
    {
        LOGV("getPreviewHeap");
        return mPreviewHeap != NULL ? mPreviewHeap->mHeap : NULL;
    }

    sp<IMemoryHeap> QualcommCameraHardware::getRawHeap() const
    {
        return mRawHeap != NULL ? mRawHeap->mHeap : NULL;
    }

    bool QualcommCameraHardware::setCallbacks(
        preview_callback pcb, void *puser,
        recording_callback rcb, void *ruser)
    {
        Mutex::Autolock cbLock(&mCallbackLock);
        mPreviewCallback = pcb;
        mPreviewCallbackCookie = puser;
        mRecordingCallback = rcb;
        mRecordingCallbackCookie = ruser;
        return mPreviewCallback != NULL ||
            mRecordingCallback != NULL;
    }

    status_t QualcommCameraHardware::startPreviewInternal(
        preview_callback pcb, void *puser,
        recording_callback rcb, void *ruser)
    {
        LOGV("startPreview E");

        if (mCameraState == QCS_PREVIEW_IN_PROGRESS) {
            LOGE("startPreview is already in progress, doing nothing.");
            // We might want to change the callback functions while preview is
            // streaming, for example to enable or disable recording.
            setCallbacks(pcb, puser, rcb, ruser);
            return NO_ERROR;
        }

        // We check for these two states explicitly because it is possible
        // for startPreview() to be called in response to a raw or JPEG
        // callback, but before we've updated the state from QCS_WAITING_RAW
        // or QCS_WAITING_JPEG to QCS_IDLE.  This is because in camera_cb(),
        // we update the state *after* we've made the callback.  See that
        // function for an explanation.

        if (mCameraState == QCS_WAITING_RAW ||
            mCameraState == QCS_WAITING_JPEG) {
            while (mCameraState != QCS_IDLE &&
                   mCameraState != QCS_ERROR) {
                LOGV("waiting for QCS_IDLE");
                mStateWait.wait(mStateLock);
            }
        }

        if (mCameraState != QCS_IDLE) {
            LOGE("startPreview X Camera state is %s, expecting QCS_IDLE!",
                getCameraStateStr(mCameraState));
            return INVALID_OPERATION;
        }

        if (!initPreview()) {
            LOGE("startPreview X initPreview failed.  Not starting preview.");
            return UNKNOWN_ERROR;
        }

        setCallbacks(pcb, puser, rcb, ruser);

        // hack to prevent first preview frame from being black
        mPreviewCount = 0;

        mCameraState = QCS_INTERNAL_PREVIEW_REQUESTED;
        camera_ret_code_type qret =
            LINK_camera_start_preview(camera_cb, this);
        if (qret == CAMERA_SUCCESS) {
            while(mCameraState != QCS_PREVIEW_IN_PROGRESS &&
                  mCameraState != QCS_ERROR) {
                LOGV("waiting for QCS_PREVIEW_IN_PROGRESS");
                mStateWait.wait(mStateLock);
            }
        }
        else {
            LOGE("startPreview failed: sensor error.");
            mCameraState = QCS_ERROR;
        }

        LOGV("startPreview X");
        return mCameraState == QCS_PREVIEW_IN_PROGRESS ?
            NO_ERROR : UNKNOWN_ERROR;
    }

    void QualcommCameraHardware::stopPreviewInternal()
    {
        LOGV("stopPreviewInternal E");

        if (mCameraState != QCS_PREVIEW_IN_PROGRESS) {
            LOGE("Preview not in progress!");
            return;
        }

        if (mAutoFocusCallback != NULL) {
            // WARNING: clear mAutoFocusCallback BEFORE you call
            // camera_stop_focus.  The CAMERA_EXIT_CB_ABORT is (erroneously)
            // delivered inline camera_stop_focus(), and we cannot acquire
            // mStateLock, because that would cause a deadlock.  In any case,
            // CAMERA_EXIT_CB_ABORT is delivered only when we call
            // camera_stop_focus.
            mAutoFocusCallback = NULL;
            LINK_camera_stop_focus();
        }

        setCallbacks(NULL, NULL, NULL, NULL);
        
        mCameraState = QCS_INTERNAL_PREVIEW_STOPPING;

        LINK_camera_stop_preview();
        while (mCameraState != QCS_IDLE &&
               mCameraState != QCS_ERROR)  {
            LOGV("waiting for QCS_IDLE");
            mStateWait.wait(mStateLock);
        }

        LOGV("stopPreviewInternal: Freeing preview heap.");
        mPreviewHeap = NULL;
        mPreviewCallback = NULL;

        LOGV("stopPreviewInternal: X Preview has stopped.");
    }

    status_t QualcommCameraHardware::startPreview(
        preview_callback pcb, void *puser)
    {
        Mutex::Autolock l(&mLock);
        Mutex::Autolock stateLock(&mStateLock);
        return startPreviewInternal(pcb, puser,
                                    mRecordingCallback,
                                    mRecordingCallbackCookie);
    }

    void QualcommCameraHardware::stopPreview() {
        LOGV("stopPreview: E");
        Mutex::Autolock l(&mLock);
        if (!setCallbacks(NULL, NULL,
                          mRecordingCallback,
                          mRecordingCallbackCookie)) {
            Mutex::Autolock statelock(&mStateLock);
            stopPreviewInternal();
        }
        LOGV("stopPreview: X");
    }

    bool QualcommCameraHardware::previewEnabled() {
        Mutex::Autolock l(&mLock);
        return mCameraState == QCS_PREVIEW_IN_PROGRESS;
    }

    status_t QualcommCameraHardware::startRecording(
        recording_callback rcb, void *ruser)
    {
        Mutex::Autolock l(&mLock);
        Mutex::Autolock stateLock(&mStateLock);
        return startPreviewInternal(mPreviewCallback,
                                    mPreviewCallbackCookie,
                                    rcb, ruser);
    }

    void QualcommCameraHardware::stopRecording() {
        LOGV("stopRecording: E");
        Mutex::Autolock l(&mLock);
        if (!setCallbacks(mPreviewCallback,
                          mPreviewCallbackCookie,
                          NULL, NULL)) {
            Mutex::Autolock statelock(&mStateLock);
            stopPreviewInternal();
        }
        LOGV("stopRecording: X");
    }

    bool QualcommCameraHardware::recordingEnabled() {
        Mutex::Autolock l(&mLock);
        Mutex::Autolock stateLock(&mStateLock);
        return mCameraState == QCS_PREVIEW_IN_PROGRESS &&
            mRecordingCallback != NULL;
    }

    void QualcommCameraHardware::releaseRecordingFrame(
        const sp<IMemory>& mem __attribute__((unused)))
    {
        Mutex::Autolock l(&mLock);
        LINK_camera_release_frame();
    }

    status_t QualcommCameraHardware::autoFocus(autofocus_callback af_cb,
                                               void *user)
    {
        LOGV("Starting auto focus.");
        Mutex::Autolock l(&mLock);
        Mutex::Autolock lock(&mStateLock);

        if (mCameraState != QCS_PREVIEW_IN_PROGRESS) {
            LOGE("Invalid camera state %s: expecting QCS_PREVIEW_IN_PROGRESS,"
                 " cannot start autofocus!",
                 getCameraStateStr(mCameraState));
            return INVALID_OPERATION;
        }

        if (mAutoFocusCallback != NULL) {
            LOGV("Auto focus is already in progress");
            return mAutoFocusCallback == af_cb ? NO_ERROR : INVALID_OPERATION;
        }

        mAutoFocusCallback = af_cb;
        mAutoFocusCallbackCookie = user;
        LINK_camera_start_focus(CAMERA_AUTO_FOCUS, camera_cb, this);
        return NO_ERROR;
    }

    status_t QualcommCameraHardware::takePicture(shutter_callback shutter_cb,
                                                 raw_callback raw_cb,
                                                 jpeg_callback jpeg_cb,
                                                 void* user)
    {
        LOGV("takePicture: E raw_cb = %p, jpeg_cb = %p",
             raw_cb, jpeg_cb);
        print_time();

        Mutex::Autolock l(&mLock);
        Mutex::Autolock stateLock(&mStateLock);

        qualcomm_camera_state last_state = mCameraState;
        if (mCameraState == QCS_PREVIEW_IN_PROGRESS) {
            stopPreviewInternal();
        }

        // We check for these two states explicitly because it is possible
        // for takePicture() to be called in response to a raw or JPEG
        // callback, but before we've updated the state from QCS_WAITING_RAW
        // or QCS_WAITING_JPEG to QCS_IDLE.  This is because in camera_cb(),
        // we update the state *after* we've made the callback.  See that
        // function for an explanation why.

        if (mCameraState == QCS_WAITING_RAW ||
            mCameraState == QCS_WAITING_JPEG) {
            while (mCameraState != QCS_IDLE &&
                   mCameraState != QCS_ERROR) {
                LOGV("waiting for QCS_IDLE");
                mStateWait.wait(mStateLock);
            }
        }

        if (mCameraState != QCS_IDLE) {
            LOGE("takePicture: %sunexpected state %d, expecting QCS_IDLE",
                 (last_state == QCS_PREVIEW_IN_PROGRESS ?
                  "(stop preview) " : ""),
                 mCameraState);
            // If we had to stop preview in order to take a picture, and
            // we failed to transition to a QCS_IDLE state, that's because
            // of an internal error.
            return last_state == QCS_PREVIEW_IN_PROGRESS ?
                UNKNOWN_ERROR :
                INVALID_OPERATION;
        }

        if (!initRaw(jpeg_cb != NULL)) {
            LOGE("initRaw failed.  Not taking picture.");
            return UNKNOWN_ERROR;
        }

        if (mCameraState != QCS_IDLE) {
            LOGE("takePicture: (init raw) "
                 "unexpected state %d, expecting QCS_IDLE",
                mCameraState);
            // If we had to stop preview in order to take a picture, and
            // we failed to transition to a QCS_IDLE state, that's because
            // of an internal error.
            return last_state == QCS_PREVIEW_IN_PROGRESS ?
                UNKNOWN_ERROR :
                INVALID_OPERATION;
        }

        {
            Mutex::Autolock cbLock(&mCallbackLock);
            mShutterCallback = shutter_cb;
            mRawPictureCallback = raw_cb;
            mJpegPictureCallback = jpeg_cb;
            mPictureCallbackCookie = user;
        }

        mCameraState = QCS_INTERNAL_RAW_REQUESTED;

        LINK_camera_take_picture(camera_cb, this);

        // It's possible for the YUV callback as well as the JPEG callbacks
        // to be invoked before we even make it here, so we check for all
        // possible result states from takePicture.

        while (mCameraState != QCS_WAITING_RAW &&
               mCameraState != QCS_WAITING_JPEG &&
               mCameraState != QCS_IDLE &&
               mCameraState != QCS_ERROR)  {
            LOGV("takePicture: waiting for QCS_WAITING_RAW or QCS_WAITING_JPEG");
            mStateWait.wait(mStateLock);
            LOGV("takePicture: woke up, state is %s",
                 getCameraStateStr(mCameraState));
        }

        LOGV("takePicture: X");
        print_time();
        return mCameraState != QCS_ERROR ?
            NO_ERROR : UNKNOWN_ERROR;
    }

    status_t QualcommCameraHardware::cancelPicture(
        bool cancel_shutter, bool cancel_raw, bool cancel_jpeg)
    {
        LOGV("cancelPicture: E cancel_shutter = %d, cancel_raw = %d, cancel_jpeg = %d",
             cancel_shutter, cancel_raw, cancel_jpeg);
        Mutex::Autolock l(&mLock);
        Mutex::Autolock stateLock(&mStateLock);

        switch (mCameraState) {
        case QCS_INTERNAL_RAW_REQUESTED:
        case QCS_WAITING_RAW:
        case QCS_WAITING_JPEG:
            LOGV("camera state is %s, stopping picture.",
                 getCameraStateStr(mCameraState));

            {
                Mutex::Autolock cbLock(&mCallbackLock);
                if (cancel_shutter) mShutterCallback = NULL;
                if (cancel_raw) mRawPictureCallback = NULL;
                if (cancel_jpeg) mJpegPictureCallback = NULL;
            }

            while (mCameraState != QCS_IDLE &&
                   mCameraState != QCS_ERROR)  {
                LOGV("cancelPicture: waiting for QCS_IDLE");
                mStateWait.wait(mStateLock);
            }
            break;
        default:
            LOGV("not taking a picture (state %s)",
                 getCameraStateStr(mCameraState));
        }

        LOGV("cancelPicture: X");
        return NO_ERROR;
    }

    status_t QualcommCameraHardware::setParameters(
        const CameraParameters& params)
    {
        LOGV("setParameters: E params = %p", &params);

        Mutex::Autolock l(&mLock);
        Mutex::Autolock lock(&mStateLock);

        // FIXME: verify params
        // yuv422sp is here only for legacy reason. Unfortunately, we release
        // the code with yuv422sp as the default and enforced setting. The
        // correct setting is yuv420sp.
        if ((strcmp(params.getPreviewFormat(), "yuv420sp") != 0) &&
                (strcmp(params.getPreviewFormat(), "yuv422sp") != 0)) {
            LOGE("Only yuv420sp preview is supported");
            return INVALID_OPERATION;
        }

        // FIXME: will this make a deep copy/do the right thing? String8 i
        // should handle it
        
        mParameters = params;
        
        // libqcamera only supports certain size/aspect ratios
        // find closest match that doesn't exceed app's request
        int width, height;
        params.getPreviewSize(&width, &height);
        LOGV("requested size %d x %d", width, height);
        preview_size_type* ps = preview_sizes;
        size_t i;
        for (i = 0; i < PREVIEW_SIZE_COUNT; ++i, ++ps) {
            if (width >= ps->width && height >= ps->height) break;
        }
        // app requested smaller size than supported, use smallest size
        if (i == PREVIEW_SIZE_COUNT) ps--;
        LOGV("actual size %d x %d", ps->width, ps->height);
        mParameters.setPreviewSize(ps->width, ps->height);

        mParameters.getPreviewSize(&mPreviewWidth, &mPreviewHeight);
        mParameters.getPictureSize(&mRawWidth, &mRawHeight);

        mPreviewWidth = (mPreviewWidth + 1) & ~1;
        mPreviewHeight = (mPreviewHeight + 1) & ~1;
        mRawHeight = (mRawHeight + 1) & ~1;
        mRawWidth = (mRawWidth + 1) & ~1;

        initCameraParameters();

        LOGV("setParameters: X mCameraState=%d", mCameraState);
        return mCameraState == QCS_IDLE ?
            NO_ERROR : UNKNOWN_ERROR;
    }

    CameraParameters QualcommCameraHardware::getParameters() const
    {
        LOGV("getParameters: EX");
        return mParameters;
    }

    static CameraInfo sCameraInfo[] = {
        {
            CAMERA_FACING_BACK,
            90,  /* orientation */
        }
    };

    extern "C" int HAL_getNumberOfCameras()
    {
        return sizeof(sCameraInfo) / sizeof(sCameraInfo[0]);
    }

    extern "C" void HAL_getCameraInfo(int cameraId, struct CameraInfo* cameraInfo)
    {
        memcpy(cameraInfo, &sCameraInfo[cameraId], sizeof(CameraInfo));
    }

    extern "C" sp<CameraHardwareInterface> HAL_openCameraHardware(int cameraId)
    {
        LOGV("openCameraHardware: call createInstance");
        return QualcommCameraHardware::createInstance();
    }

    wp<QualcommCameraHardware> QualcommCameraHardware::singleton;

    // If the hardware already exists, return a strong pointer to the current
    // object. If not, create a new hardware object, put it in the singleton,
    // and return it.
    sp<CameraHardwareInterface> QualcommCameraHardware::createInstance()
    {
        LOGV("createInstance: E");

        singleton_lock.lock();
        if (singleton != 0) {
            sp<CameraHardwareInterface> hardware = singleton.promote();
            if (hardware != 0) {
                LOGV("createInstance: X return existing hardware=%p",
                     &(*hardware));
                singleton_lock.unlock();
                return hardware;
            }
        }

        {
            struct stat st;
            int rc = stat("/dev/oncrpc", &st);
            if (rc < 0) {
                LOGV("createInstance: X failed to create hardware: %s",
                     strerror(errno));
                singleton_lock.unlock();
                return NULL;
            }
        }

        QualcommCameraHardware *cam = new QualcommCameraHardware();
        sp<QualcommCameraHardware> hardware(cam);
        singleton = hardware;
        singleton_lock.unlock();

        // initDefaultParameters() will cause the camera_cb() to be called.
        // Since the latter tries to promote the singleton object to make sure
        // it still exists, we need to call this function after we have set the
        // singleton.
        cam->initDefaultParameters();
        LOGV("createInstance: X created hardware=%p", &(*hardware));
        return hardware;
    }

    // For internal use only, hence the strong pointer to the derived type.
    sp<QualcommCameraHardware> QualcommCameraHardware::getInstance()
    {
        Mutex::Autolock singletonLock(&singleton_lock);
        sp<CameraHardwareInterface> hardware = singleton.promote();
        return (hardware != 0) ?
            sp<QualcommCameraHardware>(static_cast<QualcommCameraHardware*>
                                       (hardware.get())) :
            NULL;
    }

    void* QualcommCameraHardware::get_preview_mem(uint32_t size,
                                                  uint32_t *phy_addr,
                                                  uint32_t index)
    {
        if (mPreviewHeap != NULL && mPreviewHeap->mHeap != NULL) {
            uint8_t *base = (uint8_t *)mPreviewHeap->mHeap->base();
            if (base && size <= mPreviewHeap->mSize.len) {
                // For preview, associate the memory with the VFE task in the
                // DSP.  This way, when the DSP gets a command that has a
                // physical address, it knows which pmem region to patch it
                // against.
                uint32_t vaddr = (uint32_t)(base + size*index);

                LOGV("get_preview_mem: base %p MALLOC size %d index %d --> %p",
                     base, size, index, (void *)vaddr);
                *phy_addr = vaddr;
                return (void *)vaddr;
            }
        }
        LOGV("get_preview_mem: X NULL");
        return NULL;
    }

    void QualcommCameraHardware::free_preview_mem(uint32_t *phy_addr,
                                                  uint32_t size,
                                                  uint32_t index)
    {
        LOGV("free_preview_mem: EX NOP");
        return;
    }

    void* QualcommCameraHardware::get_raw_mem(uint32_t size,
                                                   uint32_t *phy_addr,
                                                   uint32_t index)
    {
        if (mRawHeap != NULL && mRawHeap->mHeap != NULL) {
            uint8_t *base = (uint8_t *)mRawHeap->mHeap->base();
            if (base && size <= mRawHeap->mSize.len) {
                // For raw snapshot, associate the memory with the VFE and LPM
                // tasks in the DSP.  This way, when the DSP gets a command
                // that has a physical address, it knows which pmem region to
                // patch it against.
                uint32_t vaddr = (uint32_t)(base + size*index);

                LOGV("get_raw_mem: base %p MALLOC size %d index %d --> %p",
                     base, size, index, (void *)vaddr);
                *phy_addr = vaddr;
                return (void *)vaddr;
            }
        }
        LOGV("get_raw_mem: X NULL");
        return NULL;
    }

    void QualcommCameraHardware::free_raw_mem(uint32_t *phy_addr,
                                              uint32_t size,
                                              uint32_t index)
    {
        LOGV("free_raw_mem: EX NOP");
        return;
    }

    void QualcommCameraHardware::receivePreviewFrame(camera_frame_type *frame)
    {
        Mutex::Autolock cbLock(&mCallbackLock);

        // Ignore the first frame--there is a bug in the VFE pipeline and that
        // frame may be bad.
        if (++mPreviewCount == 1) {
            LINK_camera_release_frame();
            return;
        }

        // Find the offset within the heap of the current buffer.
        ssize_t offset = (uint32_t)frame->buf_Virt_Addr;
        offset -= (uint32_t)mPreviewHeap->mHeap->base();
        ssize_t frame_size = kRawFrameHeaderSize + frame->dx * frame->dy * 2;
        if (offset + frame_size <=
                (ssize_t)mPreviewHeap->mHeap->virtualSize()) {
#if 0
            // frame->buffer includes the header, frame->buf_Virt_Addr skips it
            LOGV("PREVIEW FRAME CALLBACK "
                 "base %p addr %p offset %ld "
                 "framesz %dx%d=%ld (expect %d) rotation %d "
                 "(index %ld) size %d header_size 0x%x",
                 mPreviewHeap->mHeap->base(),
                 frame->buf_Virt_Addr,
                 offset,
                 frame->dx, frame->dy,
                 frame_size,
                 mPreviewFrameSize,
                 frame->rotation,
                 offset / frame_size,
                 mPreviewFrameSize, frame->header_size);
#endif
            offset /= frame_size;
            if (mPreviewCallback != NULL)
                mPreviewCallback(mPreviewHeap->mBuffers[offset],
                                 mPreviewCallbackCookie);
            if (mRecordingCallback != NULL)
                mRecordingCallback(mPreviewHeap->mBuffers[offset],
                                   mRecordingCallbackCookie);
            else {
                // When we are doing preview but not recording, we need to
                // release every preview frame immediately so that the next
                // preview frame is delivered.  However, when we are recording
                // (whether or not we are also streaming the preview frames to
                // the screen), we have the user explicitly release a preview
                // frame via method releaseRecordingFrame().  In this way we
                // allow a video encoder which is potentially slower than the
                // preview stream to skip frames.  Note that we call
                // LINK_camera_release_frame() in this method because we first
                // need to check to see if mPreviewCallback != NULL, which
                // requires holding mCallbackLock.
                LINK_camera_release_frame();
            }
        }
        else LOGE("Preview frame virtual address %p is out of range!",
                  frame->buf_Virt_Addr);
    }

    void
    QualcommCameraHardware::notifyShutter()
    {
        LOGV("notifyShutter: E");
        print_time();
        Mutex::Autolock lock(&mStateLock);
        if (mShutterCallback)
            mShutterCallback(mPictureCallbackCookie);
        print_time();
        LOGV("notifyShutter: X");
    }

    // Pass the pre-LPM raw picture to raw picture callback.
    // This method is called by a libqcamera thread, different from the one on
    // which startPreview() or takePicture() are called.
    void QualcommCameraHardware::receiveRawPicture(camera_frame_type *frame)
    {
        LOGV("receiveRawPicture: E");
        print_time();

        Mutex::Autolock cbLock(&mCallbackLock);

        if (mRawPictureCallback != NULL) {
            // FIXME: WHY IS buf_Virt_Addr ZERO??
            frame->buf_Virt_Addr = (uint32_t*)frame->buffer;

            // Find the offset within the heap of the current buffer.
            ssize_t offset = (uint32_t)frame->buf_Virt_Addr;
            offset -= (uint32_t)mRawHeap->mHeap->base();
            ssize_t frame_size = kRawFrameHeaderSize +
                frame->captured_dx * frame->captured_dy * 2;

            if (offset + frame_size <=
                (ssize_t)mRawHeap->mHeap->virtualSize()) {
#if 0
                // frame->buffer includes the header, frame->buf_Virt_Addr
                // skips it.
                LOGV("receiveRawPicture: RAW CALLBACK (CB %p) "
                     "base %p addr %p buffer %p offset %ld "
                     "framesz %dx%d=%ld (expect %d) rotation %d "
                     "(index %ld) size %d header_size 0x%x",
                     mRawPictureCallback,
                     mRawHeap->mHeap->base(),
                     frame->buf_Virt_Addr,
                     frame->buffer,
                     offset,
                     frame->captured_dx, frame->captured_dy,
                     frame_size,
                     mRawSize,
                     frame->rotation,
                     offset / frame_size,
                     mRawSize, frame->header_size);
#endif
                offset /= frame_size;
                mRawPictureCallback(mRawHeap->mBuffers[offset],
                                    mPictureCallbackCookie);
            }
            else LOGE("receiveRawPicture: virtual address %p is out of range!",
                      frame->buf_Virt_Addr);
        }
        else LOGV("Raw-picture callback was canceled--skipping.");

        print_time();
        LOGV("receiveRawPicture: X");
    }

    // Encode the post-LPM raw picture.
    // This method is called by a libqcamera thread, different from the one on
    // which startPreview() or takePicture() are called.

    void
    QualcommCameraHardware::receivePostLpmRawPicture(camera_frame_type *frame)
    {
        LOGV("receivePostLpmRawPicture: E");
        print_time();
        qualcomm_camera_state new_state = QCS_ERROR;

        Mutex::Autolock cbLock(&mCallbackLock);

        if (mJpegPictureCallback != NULL) {

            bool encode_location = true;

#define PARSE_LOCATION(what,type,fmt,desc) do {                                           \
                pt.what = 0;                                                              \
                const char *what##_str = mParameters.get("gps-"#what);                    \
                LOGV("receiveRawPicture: GPS PARM %s --> [%s]", "gps-"#what, what##_str); \
                if (what##_str) {                                                         \
                    type what = 0;                                                        \
                    if (sscanf(what##_str, fmt, &what) == 1)                              \
                        pt.what = what;                                                   \
                    else {                                                                \
                        LOGE("GPS " #what " %s could not"                                 \
                              " be parsed as a " #desc,                                   \
                              what##_str);                                                \
                        encode_location = false;                                          \
                    }                                                                     \
                }                                                                         \
                else {                                                                    \
                    LOGW("receiveRawPicture: GPS " #what " not specified: "               \
                          "defaulting to zero in EXIF header.");                          \
                    encode_location = false;                                              \
               }                                                                          \
            } while(0)

            PARSE_LOCATION(timestamp, long, "%ld", "long");
            if (!pt.timestamp) pt.timestamp = time(NULL);
            PARSE_LOCATION(altitude, short, "%hd", "short");
            PARSE_LOCATION(latitude, double, "%lf", "double float");
            PARSE_LOCATION(longitude, double, "%lf", "double float");

#undef PARSE_LOCATION

            if (encode_location) {
                LOGV("receiveRawPicture: setting image location ALT %d LAT %lf LON %lf",
                     pt.altitude, pt.latitude, pt.longitude);
                if (LINK_camera_set_position(&pt, NULL, NULL) != CAMERA_SUCCESS) {
                    LOGE("receiveRawPicture: camera_set_position: error");
                    /* return; */ // not a big deal
                }
            }
            else LOGV("receiveRawPicture: not setting image location");

            mJpegSize = 0;
            camera_handle.device = CAMERA_DEVICE_MEM;
            camera_handle.mem.encBuf_num =  MAX_JPEG_ENCODE_BUF_NUM;

            for (int cnt = 0; cnt < MAX_JPEG_ENCODE_BUF_NUM; cnt++) {
                camera_handle.mem.encBuf[cnt].buffer = (uint8_t *)
                    malloc(MAX_JPEG_ENCODE_BUF_LEN);
                camera_handle.mem.encBuf[cnt].buf_len =
                    MAX_JPEG_ENCODE_BUF_LEN;
                camera_handle.mem.encBuf[cnt].used_len = 0;
            } /* for */

            LINK_camera_encode_picture(frame, &camera_handle, camera_cb, this);
        }
        else {
            LOGV("JPEG callback was cancelled--not encoding image.");
            // We need to keep the raw heap around until the JPEG is fully
            // encoded, because the JPEG encode uses the raw image contained in
            // that heap.
            mRawHeap = NULL;
        }                    
        print_time();
        LOGV("receivePostLpmRawPicture: X");
    }

    void
    QualcommCameraHardware::receiveJpegPictureFragment(
        JPEGENC_CBrtnType *encInfo)
    {
        camera_encode_mem_type *enc =
            (camera_encode_mem_type *)encInfo->outPtr;
        int index = enc - camera_handle.mem.encBuf;
        uint8_t *base = (uint8_t *)mJpegHeap->mHeap->base();
        uint32_t size = encInfo->size;
        uint32_t remaining = mJpegHeap->mHeap->virtualSize();
        remaining -= mJpegSize;

        LOGV("receiveJpegPictureFragment: (index %d status %d size %d)",
             index,
             encInfo->status,
             size);

        if (size > remaining) {
            LOGE("receiveJpegPictureFragment: size %d exceeds what "
                 "remains in JPEG heap (%d), truncating",
                 size,
                 remaining);
            size = remaining;
        }

        camera_handle.mem.encBuf[index].used_len = 0;
        memcpy(base + mJpegSize, enc->buffer, size);
        mJpegSize += size;
    }

    // This method is called by a libqcamera thread, different from the one on
    // which startPreview() or takePicture() are called.

    void
    QualcommCameraHardware::receiveJpegPicture(void)
    {
        LOGV("receiveJpegPicture: E image (%d bytes out of %d)",
             mJpegSize, mJpegHeap->mBufferSize);
        print_time();
        Mutex::Autolock cbLock(&mCallbackLock);

        int index = 0;

        if (mJpegPictureCallback) {
            // The reason we do not allocate into mJpegHeap->mBuffers[offset] is
            // that the JPEG image's size will probably change from one snapshot
            // to the next, so we cannot reuse the MemoryBase object.
            sp<MemoryBase> buffer = new
                MemoryBase(mJpegHeap->mHeap,
                           index * mJpegHeap->mBufferSize +
                           mJpegHeap->mFrameOffset,
                           mJpegSize);
            
            mJpegPictureCallback(buffer, mPictureCallbackCookie);
            buffer = NULL;
        }
        else LOGV("JPEG callback was cancelled--not delivering image.");

        // NOTE: the JPEG encoder uses the raw image contained in mRawHeap, so we need
        // to keep the heap around until the encoding is complete.
        mJpegHeap = NULL;
        mRawHeap = NULL;        
        
        for (int cnt = 0; cnt < MAX_JPEG_ENCODE_BUF_NUM; cnt++) {
            if (camera_handle.mem.encBuf[cnt].buffer != NULL) {
                free(camera_handle.mem.encBuf[cnt].buffer);
                memset(camera_handle.mem.encBuf + cnt, 0,
                       sizeof(camera_encode_mem_type));
            }
        } /* for */

        print_time();
        LOGV("receiveJpegPicture: X callback done.");
    }

    struct str_map {
        const char *const desc;
        int val;
    };

    static const struct str_map wb_map[] = {
        { "auto", CAMERA_WB_AUTO },
        { "custom", CAMERA_WB_CUSTOM },
        { "incandescent", CAMERA_WB_INCANDESCENT },
        { "fluorescent", CAMERA_WB_FLUORESCENT },
        { "daylight", CAMERA_WB_DAYLIGHT },
        { "cloudy", CAMERA_WB_CLOUDY_DAYLIGHT },
        { "twilight", CAMERA_WB_TWILIGHT },
        { "shade", CAMERA_WB_SHADE },
        { NULL, 0 }
    };

    static const struct str_map effect_map[] = {
        { "off", CAMERA_EFFECT_OFF },
        { "mono", CAMERA_EFFECT_MONO },
        { "negative", CAMERA_EFFECT_NEGATIVE },
        { "solarize", CAMERA_EFFECT_SOLARIZE },
        { "pastel", CAMERA_EFFECT_PASTEL },
        { "mosaic", CAMERA_EFFECT_MOSAIC },
        { "resize", CAMERA_EFFECT_RESIZE },
        { "sepia", CAMERA_EFFECT_SEPIA },
        { "posterize", CAMERA_EFFECT_POSTERIZE },
        { "whiteboard", CAMERA_EFFECT_WHITEBOARD },
        { "blackboard", CAMERA_EFFECT_BLACKBOARD },
        { "aqua", CAMERA_EFFECT_AQUA },
        { NULL, 0 }
    };

    static const struct str_map brightness_map[] = {
        { "0", CAMERA_BRIGHTNESS_0 },
        { "1", CAMERA_BRIGHTNESS_1 },
        { "2", CAMERA_BRIGHTNESS_2 },
        { "3", CAMERA_BRIGHTNESS_3 },
        { "4", CAMERA_BRIGHTNESS_4 },
        { "5", CAMERA_BRIGHTNESS_5 },
        { "6", CAMERA_BRIGHTNESS_6 },
        { "7", CAMERA_BRIGHTNESS_7 },
        { "8", CAMERA_BRIGHTNESS_8 },
        { "9", CAMERA_BRIGHTNESS_9 },
        { "10", CAMERA_BRIGHTNESS_10 },
        { NULL, 0 }
    };

    static const struct str_map antibanding_map[] = {
        { "off", CAMERA_ANTIBANDING_OFF },
        { "50hz", CAMERA_ANTIBANDING_50HZ },
        { "60hz", CAMERA_ANTIBANDING_60HZ },
        { "auto", CAMERA_ANTIBANDING_AUTO },
        { NULL, 0 }
    };

    static const struct str_map iso_map[] = {
        { "auto", CAMERA_ISO_AUTO },
        { "high", CAMERA_ISO_HIGH },
        { NULL, 0 }
    };

    static int lookup(const struct str_map *const arr, const char *name, int def)
    {
        if (name) {
            const struct str_map * trav = arr;
            while (trav->desc) {
                if (!strcmp(trav->desc, name))
                    return trav->val;
                trav++;
            }
        }
        return def;
    }

    void QualcommCameraHardware::initCameraParameters()
    {
        LOGV("initCameraParameters: E");

        // Because libqcamera is broken, for the camera_set_parm() calls
        // QualcommCameraHardware camera_cb() is called synchronously,
        // so we cannot wait on a state change.  Also, we have to unlock
        // the mStateLock, because camera_cb() acquires it.

        startCameraIfNecessary();

#define SET_PARM(x,y) do {                                             \
        LOGV("initCameraParameters: set parm: %s, %d", #x, y);         \
        LINK_camera_set_parm (x, y, NULL, NULL);                       \
    } while(0)

        /* Preview Mode: snapshot or movie */
        SET_PARM(CAMERA_PARM_PREVIEW_MODE, CAMERA_PREVIEW_MODE_SNAPSHOT);

        /* Default Rotation - none */
        int rotation = mParameters.getInt("rotation");

        // Rotation may be negative, but may not be -1, because it has to be a
        // multiple of 90.  That's why we can still interpret -1 as an error,
        if (rotation == -1) {
            LOGV("rotation not specified or is invalid, defaulting to 0");
            rotation = 0;
        }
        else if (rotation % 90) {
            LOGE("rotation %d is not a multiple of 90 degrees!  Defaulting to zero.",
                 rotation);
            rotation = 0;
        }
        else {
            // normalize to [0 - 270] degrees
            rotation %= 360;
            if (rotation < 0) rotation += 360;
        }

        SET_PARM(CAMERA_PARM_ENCODE_ROTATION, rotation);

        SET_PARM(CAMERA_PARM_WB,
                 lookup(wb_map,
                        mParameters.get("whitebalance"),
                        CAMERA_WB_AUTO));

        SET_PARM(CAMERA_PARM_EFFECT,
                 lookup(effect_map,
                        mParameters.get("effect"),
                        CAMERA_EFFECT_OFF));

        SET_PARM(CAMERA_PARM_BRIGHTNESS,
                 lookup(brightness_map,
                        mParameters.get("exposure-offset"),
                        CAMERA_BRIGHTNESS_DEFAULT));

        SET_PARM(CAMERA_PARM_ISO,
                 lookup(iso_map,
                        mParameters.get("iso"),
                        CAMERA_ISO_AUTO));

        SET_PARM(CAMERA_PARM_ANTIBANDING,
                 lookup(antibanding_map,
                        mParameters.get("antibanding"),
                        CAMERA_ANTIBANDING_AUTO));

        int ns_mode = mParameters.getInt("nightshot-mode");
        if (ns_mode < 0) ns_mode = 0;
        SET_PARM(CAMERA_PARM_NIGHTSHOT_MODE, ns_mode);

        int luma_adaptation = mParameters.getInt("luma-adaptation");
        if (luma_adaptation < 0) luma_adaptation = 0;
        SET_PARM(CAMERA_PARM_LUMA_ADAPTATION, luma_adaptation);

#undef SET_PARM

#if 0
        /* Default Auto FPS: 30 (maximum) */
        LINK_camera_set_parm_2 (CAMERA_PARM_PREVIEW_FPS,
                                (1<<16|20), // max frame rate 30
                                (4<<16|20), // min frame rate 5
                                NULL,
                                NULL);
#endif

        int th_w, th_h, th_q;
        th_w = mParameters.getInt("jpeg-thumbnail-width");
        if (th_w < 0) LOGW("property jpeg-thumbnail-width not specified");

        th_h = mParameters.getInt("jpeg-thumbnail-height");
        if (th_h < 0) LOGW("property jpeg-thumbnail-height not specified");

        th_q = mParameters.getInt("jpeg-thumbnail-quality");
        if (th_q < 0) LOGW("property jpeg-thumbnail-quality not specified");

        if (th_w > 0 && th_h > 0 && th_q > 0) {
            LOGI("setting thumbnail dimensions to %dx%d, quality %d",
                 th_w, th_h, th_q);
            int ret = LINK_camera_set_thumbnail_properties(th_w, th_h, th_q);
            if (ret != CAMERA_SUCCESS) {
                LOGE("LINK_camera_set_thumbnail_properties returned %d", ret);
            }
        }

#if defined FEATURE_CAMERA_ENCODE_PROPERTIES
        /* Set Default JPEG encoding--this does not cause a callback */
        encode_properties.quality   = mParameters.getInt("jpeg-quality");
        if (encode_properties.quality < 0) {
            LOGW("JPEG-image quality is not specified "
                 "or is negative, defaulting to %d",
                 encode_properties.quality);
            encode_properties.quality = 100;
        }
        else LOGV("Setting JPEG-image quality to %d",
                  encode_properties.quality);
        encode_properties.format    = CAMERA_JPEG;
        encode_properties.file_size = 0x0;
        LINK_camera_set_encode_properties(&encode_properties);
#else
#warning 'FEATURE_CAMERA_ENCODE_PROPERTIES should be enabled!'
#endif


        LOGV("initCameraParameters: X");
    }

    // Called with mStateLock held!
    void QualcommCameraHardware::setCameraDimensions()
    {
        if (mCameraState != QCS_IDLE) {
            LOGE("set camera dimensions: expecting state QCS_IDLE, not %s",
                 getCameraStateStr(mCameraState));
            return;
        }

        LINK_camera_set_dimensions(mRawWidth,
                                   mRawHeight,
                                   mPreviewWidth,
                                   mPreviewHeight,
                                   NULL,
                                   NULL);
    }

    QualcommCameraHardware::qualcomm_camera_state
    QualcommCameraHardware::change_state(qualcomm_camera_state new_state,
        bool lock)
    {
        if (lock) mStateLock.lock();
        if (new_state != mCameraState) {
            // Due to the fact that we allow only one thread at a time to call
            // startPreview(), stopPreview(), or takePicture(), we know that
            // only one thread at a time may be blocked waiting for a state
            // transition on mStateWait.  That's why we signal(), not
            // broadcast().

            LOGV("state transition %s --> %s",
                 getCameraStateStr(mCameraState),
                 getCameraStateStr(new_state));

            mCameraState = new_state;
            mStateWait.signal();
        }
        if (lock) mStateLock.unlock();
        return new_state;
    }

#define CAMERA_STATE(n) case n: if(n != CAMERA_FUNC_START_PREVIEW || cb != CAMERA_EVT_CB_FRAME) LOGV("STATE %s // STATUS %d", #n, cb);
#define TRANSITION(e,s) do { \
            obj->change_state(obj->mCameraState == e ? s : QCS_ERROR); \
        } while(0)
#define TRANSITION_LOCKED(e,s) do { \
            obj->change_state((obj->mCameraState == e ? s : QCS_ERROR), false); \
        } while(0)
#define TRANSITION_ALWAYS(s) obj->change_state(s)


    // This callback is called from the destructor.
    void QualcommCameraHardware::stop_camera_cb(camera_cb_type cb,
                                                const void *client_data,
                                                camera_func_type func,
                                                int32_t parm4)
    {
        QualcommCameraHardware *obj =
            (QualcommCameraHardware *)client_data;
        switch(func) {
            CAMERA_STATE(CAMERA_FUNC_STOP)
                TRANSITION(QCS_INTERNAL_STOPPING, QCS_INIT);
            break;
        default:
            break;
        }
    }

    void QualcommCameraHardware::camera_cb(camera_cb_type cb,
                                           const void *client_data,
                                           camera_func_type func,
                                           int32_t parm4)
    {
        QualcommCameraHardware *obj =
            (QualcommCameraHardware *)client_data;

        // Promote the singleton to make sure that we do not get destroyed
        // while this callback is executing.
        if (UNLIKELY(getInstance() == NULL)) {
            LOGE("camera object has been destroyed--returning immediately");
            return;
        }

        if (cb == CAMERA_EXIT_CB_ABORT ||     /* Function aborted             */
            cb == CAMERA_EXIT_CB_DSP_ABORT || /* Abort due to DSP failure     */
            cb == CAMERA_EXIT_CB_ERROR ||     /* Failed due to resource       */
            cb == CAMERA_EXIT_CB_FAILED)      /* Execution failed or rejected */
        {
            // Autofocus failures occur relatively often and are not fatal, so
            // we do not transition to QCS_ERROR for them.
            if (func != CAMERA_FUNC_START_FOCUS) {
                LOGE("QualcommCameraHardware::camera_cb: @CAMERA_EXIT_CB_FAILURE(%d) in state %s.",
                     parm4,
                     obj->getCameraStateStr(obj->mCameraState));
                TRANSITION_ALWAYS(QCS_ERROR);
            }
        }

        switch(func) {
            // This is the commonest case.
            CAMERA_STATE(CAMERA_FUNC_START_PREVIEW)
                switch(cb) {
                case CAMERA_RSP_CB_SUCCESS:
                    TRANSITION(QCS_INTERNAL_PREVIEW_REQUESTED,
                               QCS_PREVIEW_IN_PROGRESS);
                    break;
                case CAMERA_EVT_CB_FRAME:
                    switch (obj->mCameraState) {
                    case QCS_PREVIEW_IN_PROGRESS:
                        if (parm4)
                            obj->receivePreviewFrame((camera_frame_type *)parm4);
                        break;
                    case QCS_INTERNAL_PREVIEW_STOPPING:
                        LOGE("camera cb: discarding preview frame "
                             "while stopping preview");
                        break;
                    default:
                        // transition to QCS_ERROR
                        LOGE("camera cb: invalid state %s for preview!",
                             obj->getCameraStateStr(obj->mCameraState));
                        break;
                    }
/* -- this function is called now inside of receivePreviewFrame.
                    LINK_camera_release_frame();
*/
                    break;
                default:
                    // transition to QCS_ERROR
                    LOGE("unexpected cb %d for CAMERA_FUNC_START_PREVIEW.",
                         cb);
                }
                break;
            CAMERA_STATE(CAMERA_FUNC_START)
                TRANSITION(QCS_INIT, QCS_IDLE);
                break;
/* -- this case handled in stop_camera_cb() now.
            CAMERA_STATE(CAMERA_FUNC_STOP)
                TRANSITION(QCS_INTERNAL_STOPPING, QCS_INIT);
                break;
*/
            CAMERA_STATE(CAMERA_FUNC_STOP_PREVIEW)
                TRANSITION(QCS_INTERNAL_PREVIEW_STOPPING,
                           QCS_IDLE);
                break;
            CAMERA_STATE(CAMERA_FUNC_TAKE_PICTURE)
                if (cb == CAMERA_RSP_CB_SUCCESS) {
                    TRANSITION(QCS_INTERNAL_RAW_REQUESTED,
                               QCS_WAITING_RAW);
                }
                else if (cb == CAMERA_EVT_CB_SNAPSHOT_DONE) {
                    obj->notifyShutter();
                    // Received pre-LPM raw picture. Notify callback now.
                    obj->receiveRawPicture((camera_frame_type *)parm4);
                }
                else if (cb == CAMERA_EXIT_CB_DONE) {
                    // It's important that we call receiveRawPicture() before
                    // we transition the state because another thread may be
                    // waiting in cancelPicture(), and then delete this object.
                    // If the order were reversed, we might call
                    // receiveRawPicture on a dead object.
                    LOGV("Receiving post LPM raw picture.");
                    obj->receivePostLpmRawPicture((camera_frame_type *)parm4);
                    {
                        Mutex::Autolock lock(&obj->mStateLock);
                        TRANSITION_LOCKED(QCS_WAITING_RAW,
                                          obj->mJpegPictureCallback != NULL ?
                                          QCS_WAITING_JPEG :
                                          QCS_IDLE);
                    }
                } else {  // transition to QCS_ERROR
                    if (obj->mCameraState == QCS_ERROR) {
                        LOGE("camera cb: invalid state %s for taking a picture!",
                             obj->getCameraStateStr(obj->mCameraState));
                        obj->mRawPictureCallback(NULL, obj->mPictureCallbackCookie);
                        obj->mJpegPictureCallback(NULL, obj->mPictureCallbackCookie);
                        TRANSITION_ALWAYS(QCS_IDLE);
                    }
                }
                break;
            CAMERA_STATE(CAMERA_FUNC_ENCODE_PICTURE)
                switch (cb) {
                case CAMERA_RSP_CB_SUCCESS:
                    // We already transitioned the camera state to
                    // QCS_WAITING_JPEG when we called
                    // camera_encode_picture().
                    break;
                case CAMERA_EXIT_CB_BUFFER:
                    if (obj->mCameraState == QCS_WAITING_JPEG) {
                        obj->receiveJpegPictureFragment(
                            (JPEGENC_CBrtnType *)parm4);
                    }
                    else LOGE("camera cb: invalid state %s for receiving "
                              "JPEG fragment!",
                              obj->getCameraStateStr(obj->mCameraState));
                    break;
                case CAMERA_EXIT_CB_DONE:
                    if (obj->mCameraState == QCS_WAITING_JPEG) {
                        // Receive the last fragment of the image.
                        obj->receiveJpegPictureFragment(
                            (JPEGENC_CBrtnType *)parm4);

                        // The size of the complete JPEG image is in 
                        // mJpegSize.

                        // It's important that we call receiveJpegPicture()
                        // before we transition the state because another
                        // thread may be waiting in cancelPicture(), and then
                        // delete this object.  If the order were reversed, we
                        // might call receiveRawPicture on a dead object.

                        obj->receiveJpegPicture();

                        TRANSITION(QCS_WAITING_JPEG, QCS_IDLE);
                    }
                    // transition to QCS_ERROR
                    else LOGE("camera cb: invalid state %s for "
                              "receiving JPEG!",
                              obj->getCameraStateStr(obj->mCameraState));
                    break;
                default:
                    // transition to QCS_ERROR
                    LOGE("camera cb: unknown cb %d for JPEG!", cb);
                }
            break;
            CAMERA_STATE(CAMERA_FUNC_START_FOCUS) {
                // NO TRANSITION HERE.  We acquire mStateLock here because it is
                // possible for ::autoFocus to be called after the call to
                // mAutoFocusCallback() but before we set mAutoFocusCallback
                // to NULL.
                if (obj->mAutoFocusCallback) {
                    switch (cb) {
                    case CAMERA_RSP_CB_SUCCESS:
                        LOGV("camera cb: autofocus has started.");
                        break;
                    case CAMERA_EXIT_CB_DONE: {
                        LOGV("camera cb: autofocus succeeded.");
                        Mutex::Autolock lock(&obj->mStateLock);
                        if (obj->mAutoFocusCallback) {
                            obj->mAutoFocusCallback(true,
                                    obj->mAutoFocusCallbackCookie);
                            obj->mAutoFocusCallback = NULL;
                        }
                    }
                        break;
                    case CAMERA_EXIT_CB_ABORT:
                        LOGE("camera cb: autofocus aborted");
                        break;
                    case CAMERA_EXIT_CB_FAILED: {
                        LOGE("camera cb: autofocus failed");
                        Mutex::Autolock lock(&obj->mStateLock);
                        if (obj->mAutoFocusCallback) {
                            obj->mAutoFocusCallback(false,
                                    obj->mAutoFocusCallbackCookie);
                            obj->mAutoFocusCallback = NULL;
                        }
                    }
                        break;
                    default:
                        LOGE("camera cb: unknown cb %d for "
                             "CAMERA_FUNC_START_FOCUS!", cb);
                    }
                }
            } break;
        default:
            // transition to QCS_ERROR
            LOGE("Unknown camera-callback status %d", cb);
        }
    }

#undef TRANSITION
#undef TRANSITION_LOCKED
#undef TRANSITION_ALWAYS
#undef CAMERA_STATE

    static unsigned clp2(unsigned x) {
        x = x - 1;
        x = x | (x >> 1);
        x = x | (x >> 2);
        x = x | (x >> 4);
        x = x | (x >> 8);
        x = x | (x >>16);
        return x + 1;
    }

    QualcommCameraHardware::MemPool::MemPool(int buffer_size, int num_buffers,
                                             int frame_size,
                                             int frame_offset,
                                             const char *name) :
        mBufferSize(buffer_size),
        mNumBuffers(num_buffers),
        mFrameSize(frame_size),
        mFrameOffset(frame_offset),
        mBuffers(NULL), mName(name)
    {
        // empty
    }

    void QualcommCameraHardware::MemPool::completeInitialization()
    {
        // If we do not know how big the frame will be, we wait to allocate
        // the buffers describing the individual frames until we do know their
        // size.

        if (mFrameSize > 0) {
            mBuffers = new sp<MemoryBase>[mNumBuffers];
            for (int i = 0; i < mNumBuffers; i++) {
                mBuffers[i] = new
                    MemoryBase(mHeap,
                               i * mBufferSize + mFrameOffset,
                               mFrameSize);
            }
        }
    }

    QualcommCameraHardware::AshmemPool::AshmemPool(int buffer_size, int num_buffers,
                                                   int frame_size,
                                                   int frame_offset,
                                                   const char *name) :
        QualcommCameraHardware::MemPool(buffer_size,
                                        num_buffers,
                                        frame_size,
                                        frame_offset,
                                        name)
    {
            LOGV("constructing MemPool %s backed by ashmem: "
                 "%d frames @ %d bytes, offset %d, "
                 "buffer size %d",
                 mName,
                 num_buffers, frame_size, frame_offset, buffer_size);

            int page_mask = getpagesize() - 1;
            int ashmem_size = buffer_size * num_buffers;
            ashmem_size += page_mask;
            ashmem_size &= ~page_mask;

            mHeap = new MemoryHeapBase(ashmem_size);

            completeInitialization();
    }

    QualcommCameraHardware::PmemPool::PmemPool(const char *pmem_pool,
                                               int buffer_size, int num_buffers,
                                               int frame_size,
                                               int frame_offset,
                                               const char *name) :
        QualcommCameraHardware::MemPool(buffer_size,
                                        num_buffers,
                                        frame_size,
                                        frame_offset,
                                        name)
    {
        LOGV("constructing MemPool %s backed by pmem pool %s: "
             "%d frames @ %d bytes, offset %d, buffer size %d",
             mName,
             pmem_pool, num_buffers, frame_size, frame_offset,
             buffer_size);
        
        // Make a new mmap'ed heap that can be shared across processes.
        
        mAlignedSize = clp2(buffer_size * num_buffers);
        
        sp<MemoryHeapBase> masterHeap = 
            new MemoryHeapBase(pmem_pool, mAlignedSize, 0);
        sp<MemoryHeapPmem> pmemHeap = new MemoryHeapPmem(masterHeap, 0);
        if (pmemHeap->getHeapID() >= 0) {
            pmemHeap->slap();
            masterHeap.clear();
            mHeap = pmemHeap;
            pmemHeap.clear();
            
            mFd = mHeap->getHeapID();
            if (::ioctl(mFd, PMEM_GET_SIZE, &mSize)) {
                LOGE("pmem pool %s ioctl(PMEM_GET_SIZE) error %s (%d)",
                     pmem_pool,
                     ::strerror(errno), errno);
                mHeap.clear();
                return;
            }
            
            LOGV("pmem pool %s ioctl(PMEM_GET_SIZE) is %ld",
                 pmem_pool,
                 mSize.len);
            
            completeInitialization();
        }
        else LOGE("pmem pool %s error: could not create master heap!",
                  pmem_pool);
    }

    QualcommCameraHardware::PreviewPmemPool::PreviewPmemPool(
            int buffer_size, int num_buffers,
            int frame_size,
            int frame_offset,
            const char *name) :
        QualcommCameraHardware::PmemPool("/dev/pmem_adsp",
                                         buffer_size,
                                         num_buffers,
                                         frame_size,
                                         frame_offset,
                                         name)
    {
        LOGV("constructing PreviewPmemPool");
        if (initialized()) {
            LINK_camera_assoc_pmem(QDSP_MODULE_VFETASK,
                                   mFd,
                                   mHeap->base(),
                                   mAlignedSize,
                                   0); // external
        }
    }

    QualcommCameraHardware::PreviewPmemPool::~PreviewPmemPool()
    {
        LOGV("destroying PreviewPmemPool");
        if(initialized()) {
            void *base = mHeap->base();
            LOGV("releasing PreviewPmemPool memory %p from module %d",
                 base, QDSP_MODULE_VFETASK);
            LINK_camera_release_pmem(QDSP_MODULE_VFETASK, base,
                                     mAlignedSize,
                                     true);
        }
    }

    QualcommCameraHardware::RawPmemPool::RawPmemPool(
            const char *pmem_pool,
            int buffer_size, int num_buffers,
            int frame_size,
            int frame_offset,
            const char *name) :
        QualcommCameraHardware::PmemPool(pmem_pool,
                                         buffer_size,
                                         num_buffers,
                                         frame_size,
                                         frame_offset,
                                         name)
    {
        LOGV("constructing RawPmemPool");

        if (initialized()) {
            LINK_camera_assoc_pmem(QDSP_MODULE_VFETASK,
                                   mFd,
                                   mHeap->base(),
                                   mAlignedSize,
                                   0); // do not free, main module
            LINK_camera_assoc_pmem(QDSP_MODULE_LPMTASK,
                                   mFd,
                                   mHeap->base(),
                                   mAlignedSize,
                                   2); // do not free, dependent module
            LINK_camera_assoc_pmem(QDSP_MODULE_JPEGTASK,
                                   mFd,
                                   mHeap->base(),
                                   mAlignedSize,
                                   2); // do not free, dependent module
        }
    }

    QualcommCameraHardware::RawPmemPool::~RawPmemPool()
    {
        LOGV("destroying RawPmemPool");
        if(initialized()) {
            void *base = mHeap->base();
            LOGV("releasing RawPmemPool memory %p from modules %d, %d, and %d",
                 base, QDSP_MODULE_VFETASK, QDSP_MODULE_LPMTASK,
                 QDSP_MODULE_JPEGTASK);
            LINK_camera_release_pmem(QDSP_MODULE_VFETASK,
                                     base, mAlignedSize, true);
            LINK_camera_release_pmem(QDSP_MODULE_LPMTASK, 
                                     base, mAlignedSize, true);
            LINK_camera_release_pmem(QDSP_MODULE_JPEGTASK,
                                     base, mAlignedSize, true);
        }
    }
    
    QualcommCameraHardware::MemPool::~MemPool()
    {
        LOGV("destroying MemPool %s", mName);
        if (mFrameSize > 0)
            delete [] mBuffers;
        mHeap.clear();
        LOGV("destroying MemPool %s completed", mName);        
    }
    
    status_t QualcommCameraHardware::MemPool::dump(int fd, const Vector<String16>& args) const
    {
        const size_t SIZE = 256;
        char buffer[SIZE];
        String8 result;
        snprintf(buffer, 255, "QualcommCameraHardware::AshmemPool::dump\n");
        result.append(buffer);
        if (mName) {
            snprintf(buffer, 255, "mem pool name (%s)\n", mName);
            result.append(buffer);
        }
        if (mHeap != 0) {
            snprintf(buffer, 255, "heap base(%p), size(%d), flags(%d), device(%s)\n",
                     mHeap->getBase(), mHeap->getSize(),
                     mHeap->getFlags(), mHeap->getDevice());
            result.append(buffer);
        }
        snprintf(buffer, 255, "buffer size (%d), number of buffers (%d),"
                 " frame size(%d), and frame offset(%d)\n",
                 mBufferSize, mNumBuffers, mFrameSize, mFrameOffset);
        result.append(buffer);
        write(fd, result.string(), result.size());
        return NO_ERROR;
    }
    
    static uint8_t* malloc_preview(uint32_t size,
            uint32_t *phy_addr, uint32_t index)
    {
        sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
        if (obj != 0) {
            return (uint8_t *)obj->get_preview_mem(size, phy_addr, index);
        }
        return NULL;
    }

    static int free_preview(uint32_t *phy_addr, uint32_t size,
                            uint32_t index)
    {
        sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
        if (obj != 0) {
            obj->free_preview_mem(phy_addr, size, index);
        }
        return 0;
    }

    static uint8_t* malloc_raw(uint32_t size,
                                  uint32_t *phy_addr,
                                  uint32_t index)
    {
        sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
        if (obj != 0) {
            return (uint8_t *)obj->get_raw_mem(size, phy_addr, index);
        }
        return NULL;
    }

    static int free_raw(uint32_t *phy_addr,
                        uint32_t size,
                        uint32_t index)
    {
        sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
        if (obj != 0) {
            obj->free_raw_mem(phy_addr, size, index);
        }
        return 0;
    }

    static void cb_rex_signal_ready(void)
    {
        LOGV("Received REX-ready signal.");
        rex_init_lock.lock();
        rex_init_wait.broadcast();
        rex_init_lock.unlock();
    }

    const char* const QualcommCameraHardware::getCameraStateStr(
        QualcommCameraHardware::qualcomm_camera_state s)
    {
        static const char* states[] = {
#define STATE_STR(x) #x
            STATE_STR(QCS_INIT),
            STATE_STR(QCS_IDLE),
            STATE_STR(QCS_ERROR),
            STATE_STR(QCS_PREVIEW_IN_PROGRESS),
            STATE_STR(QCS_WAITING_RAW),
            STATE_STR(QCS_WAITING_JPEG),
            STATE_STR(QCS_INTERNAL_PREVIEW_STOPPING),
            STATE_STR(QCS_INTERNAL_PREVIEW_REQUESTED),
            STATE_STR(QCS_INTERNAL_RAW_REQUESTED),
            STATE_STR(QCS_INTERNAL_STOPPING),
#undef STATE_STR
        };
        return states[s];
    }

}; // namespace android
