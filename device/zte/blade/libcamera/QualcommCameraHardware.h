/*
** Copyright 2008, Google Inc.
** Copyright (c) 2009, Code Aurora Forum. All rights reserved.
** Copyright (c) 2010, Ricardo Cerqueira
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
**
** NOTICE - (RC)
**
** All alterations done to this file to add support for the Z71 terminal
** are intended for use with CyanogenMod. This includes all the support
** for ov5642, and the reverse engineered bits like ioctls and EXIF 
** referred to below as "Values originally from proprietary headers")
** Please do not change the EXIF header without asking me first.
*/

#ifndef ANDROID_HARDWARE_QUALCOMM_CAMERA_HARDWARE_H
#define ANDROID_HARDWARE_QUALCOMM_CAMERA_HARDWARE_H

#include <camera/CameraHardwareInterface.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <stdint.h>
#include <ui/Overlay.h>

extern "C" {
#include <linux/android_pmem.h>
#include <msm_camera.h>
}

struct str_map {
    const char *const desc;
    int val;
};

typedef enum {
    TARGET_MSM7625,
    TARGET_MSM7627,
    TARGET_QSD8250,
    TARGET_MSM7630,
    TARGET_MAX
}targetType;

struct target_map {
    const char *targetStr;
    targetType targetEnum;
};

struct board_property{
    targetType target;
    unsigned int previewSizeMask;
};

/* Values originally in proprietary headers */

#define MSM_CAMERA_CONTROL "/dev/msm_camera/control0"

#define TRUE 1
#define FALSE 0

#define CAMERA_MIN_CONTRAST 0
#define CAMERA_MAX_CONTRAST 4
#define CAMERA_MIN_SHARPNESS 0
#define CAMERA_MIN_EXPOSURE_COMPENSATION -2
#define CAMERA_MAX_SHARPNESS 4
#define CAMERA_MIN_SATURATION 0
#define CAMERA_MAX_SATURATION 4
#define CAMERA_MAX_EXPOSURE_COMPENSATION 2
#define CAMERA_DEF_SHARPNESS 2
#define CAMERA_DEF_CONTRAST 2
#define CAMERA_DEF_SATURATION 2
#define CAMERA_DEF_EXPOSURE_COMPENSATION "0"
#define CAMERA_EXPOSURE_COMPENSATION_STEP 1

#define CEILING16(x) (x&0xfffffff0)
#define PAD_TO_WORD(x) ((x&1) ? x+1 : x)

#define JPEG_EVENT_DONE 0
#define CAM_CTRL_SUCCESS 1

#define MINIMUM_FPS 10
#define DEFAULT_FPS 15
#define MAXIMUM_FPS 25

typedef struct {
	unsigned int in1_w;
	unsigned int in1_h;
	unsigned int out1_w;
	unsigned int out1_h;
	unsigned int in2_w;
	unsigned int in2_h;
	unsigned int out2_w;
	unsigned int out2_h;
	uint8_t update_flag; 
} common_crop_t;

typedef uint8_t cam_ctrl_type;

typedef struct {
	unsigned short video_width;
	unsigned short video_height;
	unsigned short picture_width;
	unsigned short picture_height;
	unsigned short display_width;
	unsigned short display_height;
	unsigned short orig_picture_dx;
	unsigned short orig_picture_dy;
	unsigned short ui_thumbnail_height;
	unsigned short ui_thumbnail_width;
	unsigned short thumbnail_width;
	unsigned short thumbnail_height;
	unsigned short raw_picture_height;
	unsigned short raw_picture_width;
	unsigned short filler7;
	unsigned short filler8;
} cam_ctrl_dimension_t;

typedef struct {
	uint32_t timestamp;  /* seconds since 1/6/1980          */
	double   latitude;   /* degrees, WGS ellipsoid */
	double   longitude;  /* degrees                */
	int16_t  altitude;   /* meters                          */
} camera_position_type;
typedef uint8_t jpeg_event_t;

typedef enum {
	CAMERA_WB_MIN_MINUS_1,
	CAMERA_WB_AUTO = 1,  /* This list must match aeecamera.h */
	CAMERA_WB_CUSTOM,
	CAMERA_WB_INCANDESCENT,
	CAMERA_WB_FLUORESCENT,
	CAMERA_WB_DAYLIGHT,
	CAMERA_WB_CLOUDY_DAYLIGHT,
	CAMERA_WB_TWILIGHT,
	CAMERA_WB_SHADE,
	CAMERA_WB_MAX_PLUS_1
} camera_wb_type;

typedef enum {
    CAMERA_ANTIBANDING_OFF,
    CAMERA_ANTIBANDING_60HZ,
    CAMERA_ANTIBANDING_50HZ,
    CAMERA_ANTIBANDING_AUTO,
    CAMERA_MAX_ANTIBANDING,
} camera_antibanding_type;

typedef enum {
	AF_MODE_NORMAL,
	AF_MODE_MACRO,
	AF_MODE_AUTO,
} isp3a_af_mode_t;

enum {
	CAMERA_AEC_FRAME_AVERAGE,
	CAMERA_AEC_CENTER_WEIGHTED,
	CAMERA_AEC_SPOT_METERING,
};

enum {
	LED_MODE_OFF,
	LED_MODE_AUTO,
	LED_MODE_ON,
};

typedef enum {
	CAMERA_ISO_AUTO,
	CAMERA_ISO_DEBLUR,
	CAMERA_ISO_100,
	CAMERA_ISO_200,
	CAMERA_ISO_400,
	CAMERA_ISO_800,
	CAMERA_ISO_1600,
} camera_iso_mode_type;

struct fifo_queue {
	int num_of_frames;
	int front;
	struct fifo_node *node;
	pthread_mutex_t mut;
	pthread_cond_t wait;
};

struct fifo_node {
	struct msm_frame *f;
	struct fifo_node *next;
};

void enqueue(struct fifo_queue *queue, struct fifo_node *node) {
	struct fifo_node *cur_node=queue->node;
	int i;
	LOGE("enqueue:%p(%d)\n", node, queue->num_of_frames);
	node->next=NULL;
	if(queue->num_of_frames==0) {
		queue->num_of_frames++;
		queue->front=!!queue->num_of_frames;
		queue->node=node;
		return;
	}
	queue->num_of_frames++;
	queue->front=!!queue->num_of_frames;
	for(i=0;i<(queue->num_of_frames-2);++i) {
		cur_node=cur_node->next;
		assert(!!cur_node);
	}
	cur_node->next=node;
}

struct fifo_node *dequeue(struct fifo_queue *queue) {
	if(queue->num_of_frames==0)
		return NULL;
	struct fifo_node *node=queue->node;
	LOGE("dequeue:%p(%d)\n", node, queue->num_of_frames);
	queue->num_of_frames--;
	queue->front=!!queue->num_of_frames;
	queue->node=queue->node->next;
	return node;
}


enum camera_ops {
    CAMERA_SET_PARM_ENCODE_ROTATION,
    CAMERA_SET_PARM_DIMENSION,
    CAMERA_SET_PARM_ZOOM,
    CAMERA_SET_PARM_SENSOR_POSITION,
    CAMERA_SET_PARM_SHARPNESS,
    CAMERA_SET_PARM_LUMA_ADAPTATION,
    CAMERA_SET_PARM_CONTRAST,
    CAMERA_SET_PARM_EXPOSURE_COMPENSATION,
    CAMERA_SET_PARM_BRIGHTNESS,
    CAMERA_SET_PARM_FOCUS_RECT,
    CAMERA_SET_PARM_HUE,
    CAMERA_SET_PARM_SATURATION,
    CAMERA_SET_PARM_EXPOSURE,
    CAMERA_SET_PARM_AUTO_FOCUS,
    CAMERA_SET_PARM_WB,
    CAMERA_SET_PARM_EFFECT,
    CAMERA_SET_PARM_FPS,
    CAMERA_SET_PARM_FLASH,
    CAMERA_SET_PARM_NIGHTSHOT_MODE,
    CAMERA_SET_PARM_REFLECT,
    CAMERA_SET_PARM_PREVIEW_MODE,
    CAMERA_SET_PARM_ANTIBANDING,
    CAMERA_SET_PARM_RED_EYE_REDUCTION,
    CAMERA_SET_PARM_FOCUS_STEP,
    CAMERA_SET_PARM_EXPOSURE_METERING,
    CAMERA_SET_PARM_AUTO_EXPOSURE_MODE,
    CAMERA_SET_PARM_ISO,
    CAMERA_SET_PARM_BESTSHOT_MODE,
    CAMERA_SET_PARM_PREVIEW_FPS,
    CAMERA_SET_PARM_AF_MODE,
    CAMERA_SET_PARM_HISTOGRAM,
    CAMERA_SET_PARM_FLASH_STATE,
    CAMERA_SET_PARM_FRAME_TIMESTAMP,
    CAMERA_SET_PARM_STROBE_FLASH,
    CAMERA_SET_PARM_FPS_LIST,
    CAMERA_SET_PARM_HJR,
    CAMERA_SET_PARM_ROLLOFF=37,
    CAMERA_STOP_PREVIEW=38,
    CAMERA_START_PREVIEW,
    CAMERA_START_SNAPSHOT,
    CAMERA_START_VIDEO,
    CAMERA_STOP_SNAPSHOT=42,
    CAMERA_EXIT=43,
    CAMERA_STOP_VIDEO,
    CAMERA_START_RECORDING,
    CAMERA_STOP_RECORDING,
    CAMERA_GET_PARM_MAXZOOM,
    CAMERA_START_RAW_SNAPSHOT,
    CAMERA_SET_PARM_LED_MODE,
    CAMERA_GET_PARM_AF_SHARPNESS,
    CAMERA_SET_MOTION_ISO,
    CAMERA_AUTO_FOCUS_CANCEL,
    CAMERA_GET_PARM_FOCUS_STEP,
    CAMERA_ENABLE_AFD,
    CAMERA_PREPARE_SNAPSHOT,
    CAMERA_SET_FPS_MODE,
    CAMERA_SET_PARM_SCENE_MODE,
};

typedef enum {
	CAMERA_RSP_CB_SUCCESS,
	CAMERA_EXIT_CB_DONE,
	CAMERA_EXIT_CB_FAILED,
	CAMERA_EXIT_CB_DSP_IDLE,
	CAMERA_EXIT_CB_DSP_ABORT,
	CAMERA_EXIT_CB_ABORT,
	CAMERA_EXIT_CB_ERROR,
	CAMERA_EVT_CB_FRAME,
	CAMERA_EVT_CB_PICTURE,
	CAMERA_STATUS_CB,
	CAMERA_EXIT_CB_FILE_SIZE_EXCEEDED,
	CAMERA_EXIT_CB_BUFFER,
	CAMERA_EVT_CB_SNAPSHOT_DONE,
	CAMERA_CB_MAX,
} camera_cb_type;

struct cam_frame_start_parms {
	unsigned int unknown;
	struct msm_frame frame;
	struct msm_frame video_frame;
};

typedef unsigned int exif_tag_id_t;

#define EXIF_RATIONAL 5
#define EXIF_ASCII 2
#define EXIF_BYTE 1

typedef struct {
	int val;
	int otherval;
} rat_t;


typedef union {
        char * _ascii; /* At byte 16 relative to exif_tag_entry_t */
        rat_t * _rats;
        rat_t  _rat;
        uint8_t _byte;
} exif_tag_data_t;

/* The entire exif_tag_entry_t struct must be 24 bytes in length */
typedef unsigned int exif_tag_type_t;
typedef struct {
	exif_tag_type_t type;
	uint32_t copy;
	uint32_t count;
	exif_tag_data_t data;
} exif_tag_entry_t;

typedef struct {
	exif_tag_id_t tag_id;
	exif_tag_entry_t tag_entry;
} exif_tags_info_t;

/* EXIF tag IDs */
#define EXIFTAGID_GPS_LATITUDE 0x20002
#define EXIFTAGID_GPS_LATITUDE_REF 0x10001
#define EXIFTAGID_GPS_LONGITUDE 0x40004
#define EXIFTAGID_GPS_LONGITUDE_REF 0x30003
#define EXIFTAGID_GPS_ALTITUDE 0x60006
#define EXIFTAGID_GPS_ALTITUDE_REF 0x50005
#define EXIFTAGID_EXIF_CAMERA_MAKER 0x21010F
#define EXIFTAGID_EXIF_CAMERA_MODEL 0x220110
#define EXIFTAGID_EXIF_DATE_TIME_ORIGINAL 0x3A9003
#define EXIFTAGID_EXIF_DATE_TIME 0x3B9004
/* End of values originally in proprietary headers */

namespace android {

class QualcommCameraHardware : public CameraHardwareInterface {
public:

    virtual sp<IMemoryHeap> getPreviewHeap() const;
    virtual sp<IMemoryHeap> getRawHeap() const;

    virtual void setCallbacks(notify_callback notify_cb,
                              data_callback data_cb,
                              data_callback_timestamp data_cb_timestamp,
                              void* user);
    virtual void enableMsgType(int32_t msgType);
    virtual void disableMsgType(int32_t msgType);
    virtual bool msgTypeEnabled(int32_t msgType);

    virtual status_t dump(int fd, const Vector<String16>& args) const;
    virtual status_t startPreview();
    virtual void stopPreview();
    virtual bool previewEnabled();
    virtual status_t startRecording();
    virtual void stopRecording();
    virtual bool recordingEnabled();
    virtual void releaseRecordingFrame(const sp<IMemory>& mem);
    virtual status_t autoFocus();
    virtual status_t cancelAutoFocus();
    virtual status_t takePicture();
    virtual status_t cancelPicture();
    virtual status_t setParameters(const CameraParameters& params);
    virtual CameraParameters getParameters() const;
    virtual status_t sendCommand(int32_t command, int32_t arg1, int32_t arg2);
    virtual status_t getBufferInfo( sp<IMemory>& Frame, size_t *alignedSize);

    virtual void release();
    virtual bool useOverlay();
    virtual status_t setOverlay(const sp<Overlay> &overlay);

    static sp<CameraHardwareInterface> createInstance();
    static sp<QualcommCameraHardware> getInstance();

    void receivePreviewFrame(struct msm_frame *frame);
    void receiveRecordingFrame(struct msm_frame *frame);
    void receiveJpegPicture(void);
    void jpeg_set_location();
    void receiveJpegPictureFragment(uint8_t *buf, uint32_t size);
    void notifyShutter(common_crop_t *crop);
    void receive_camframetimeout();

private:
    QualcommCameraHardware();
    virtual ~QualcommCameraHardware();
    status_t startPreviewInternal();
    void stopPreviewInternal();
    friend void *auto_focus_thread(void *user);
    void runAutoFocus();
    status_t cancelAutoFocusInternal();
    bool native_set_dimension (int camfd);
    bool native_jpeg_encode (void);
    bool native_set_parm(cam_ctrl_type type, uint16_t length, void *value);
    bool native_zoom_image(int fd, int srcOffset, int dstOffset, common_crop_t *crop);

    static wp<QualcommCameraHardware> singleton;

    /* These constants reflect the number of buffers that libmmcamera requires
       for preview and raw, and need to be updated when libmmcamera
       changes.
    */
    static const int kPreviewBufferCount = NUM_PREVIEW_BUFFERS;
    static const int kRawBufferCount = 1;
    static const int kJpegBufferCount = 1;

    int jpegPadding;

    CameraParameters mParameters;
    unsigned int frame_size;
    bool mCameraRunning;
    Mutex mCameraRunningLock;
    bool mPreviewInitialized;

    // This class represents a heap which maintains several contiguous
    // buffers.  The heap may be backed by pmem (when pmem_pool contains
    // the name of a /dev/pmem* file), or by ashmem (when pmem_pool == NULL).

    struct MemPool : public RefBase {
        MemPool(int buffer_size, int num_buffers,
                int frame_size,
                const char *name);

        virtual ~MemPool() = 0;

        void completeInitialization();
        bool initialized() const {
            return mHeap != NULL && mHeap->base() != MAP_FAILED;
        }

        virtual status_t dump(int fd, const Vector<String16>& args) const;

        int mBufferSize;
        int mAlignedBufferSize;
        int mNumBuffers;
        int mFrameSize;
        sp<MemoryHeapBase> mHeap;
        sp<MemoryBase> *mBuffers;

        const char *mName;
    };

    struct AshmemPool : public MemPool {
        AshmemPool(int buffer_size, int num_buffers,
                   int frame_size,
                   const char *name);
    };

    struct PmemPool : public MemPool {
        PmemPool(const char *pmem_pool,
                 int control_camera_fd, int flags, int pmem_type,
                 int buffer_size, int num_buffers,
                 int frame_size,
                 const char *name);
        virtual ~PmemPool();
        int mFd;
        int mPmemType;
        int mCameraControlFd;
        uint32_t mAlignedSize;
        struct pmem_region mSize;
    };

    sp<PmemPool> mPreviewHeap;
    sp<PmemPool> mRecordHeap;
    sp<PmemPool> mThumbnailHeap;
    sp<PmemPool> mRawHeap;
    sp<PmemPool> mDisplayHeap;
    sp<AshmemPool> mJpegHeap;
    sp<PmemPool> mRawSnapShotPmemHeap;
    sp<PmemPool> mPostViewHeap;


    bool startCamera();
    bool initPreview();
    bool initRecord();
    void deinitPreview();
    bool initRaw(bool initJpegHeap);
    bool initRawSnapshot();
    void deinitRaw();
    void deinitRawSnapshot();

    bool mFrameThreadRunning;
    Mutex mFrameThreadWaitLock;
    Condition mFrameThreadWait;
    friend void *frame_thread(void *user);
    void runFrameThread(void *data);

    //720p recording video thread
    bool mVideoThreadExit;
    bool mVideoThreadRunning;
    Mutex mVideoThreadWaitLock;
    Condition mVideoThreadWait;
    friend void *video_thread(void *user);
    void runVideoThread(void *data);


    bool mShutterPending;
    Mutex mShutterLock;

    bool mSnapshotThreadRunning;
    Mutex mSnapshotThreadWaitLock;
    Condition mSnapshotThreadWait;
    friend void *snapshot_thread(void *user);
    void runSnapshotThread(void *data);
    Mutex mRawPictureHeapLock;
    bool mJpegThreadRunning;
    Mutex mJpegThreadWaitLock;
    Condition mJpegThreadWait;
    bool mInSnapshotMode;
    Mutex mInSnapshotModeWaitLock;
    Condition mInSnapshotModeWait;

    void debugShowPreviewFPS() const;
    void debugShowVideoFPS() const;

    int mSnapshotFormat;
    void filterPictureSizes();
    void filterPreviewSizes();
    void storeTargetType();

    void initDefaultParameters();
    void findSensorType();

    status_t setPreviewSize(const CameraParameters& params);
    status_t setPreviewFrameRate(const CameraParameters& params);
    status_t setPictureSize(const CameraParameters& params);
    status_t setJpegQuality(const CameraParameters& params);
    status_t setAntibanding(const CameraParameters& params);
    status_t setEffect(const CameraParameters& params);
    status_t setAutoExposure(const CameraParameters& params);
    status_t setWhiteBalance(const CameraParameters& params);
    status_t setFlash(const CameraParameters& params);
    status_t setGpsLocation(const CameraParameters& params);
    status_t setRotation(const CameraParameters& params);
    status_t setZoom(const CameraParameters& params);
    status_t setFocusMode(const CameraParameters& params);
    status_t setBrightness(const CameraParameters& params);
    status_t setExposureCompensation(const CameraParameters& params);
    status_t setOrientation(const CameraParameters& params);
    status_t setLensshadeValue(const CameraParameters& params);
    status_t setISOValue(const CameraParameters& params);
    status_t setPictureFormat(const CameraParameters& params);
    status_t setSharpness(const CameraParameters& params);
    status_t setContrast(const CameraParameters& params);
    status_t setSaturation(const CameraParameters& params);
    void setGpsParameters();
    void storePreviewFrameForPostview();
    bool isValidDimension(int w, int h);

    Mutex mLock;
    Mutex mCamframeTimeoutLock;
    bool camframe_timeout_flag;
    bool mReleasedRecordingFrame;

    void receiveRawPicture(void);
    void receiveRawSnapshot(void);

    Mutex mCallbackLock;
    Mutex mOverlayLock;
	Mutex mRecordLock;
	Mutex mRecordFrameLock;
	Condition mRecordWait;
    Condition mStateWait;

    /* mJpegSize keeps track of the size of the accumulated JPEG.  We clear it
       when we are about to take a picture, so at any time it contains either
       zero, or the size of the last JPEG picture taken.
    */
    uint32_t mJpegSize;
    unsigned int        mPreviewFrameSize;
    unsigned int        mRecordFrameSize;
    int                 mRawSize;
    int                 mJpegMaxSize;

#if DLOPEN_LIBMMCAMERA
    void *libmmcamera;
#endif

    int mCameraControlFd;
    struct msm_camsensor_info mSensorInfo;
    cam_ctrl_dimension_t mDimension;
    bool mAutoFocusThreadRunning;
    Mutex mAutoFocusThreadLock;
    int mAutoFocusFd;

    Mutex mAfLock;

    pthread_t mFrameThread;
    pthread_t mVideoThread;
    pthread_t mSnapshotThread;

    common_crop_t mCrop;

    bool mInitialized;

    int mBrightness;
    int mHJR;
    struct msm_frame frames[kPreviewBufferCount];
    struct msm_frame *recordframes;
    bool mInPreviewCallback;
    bool mUseOverlay;
    sp<Overlay>  mOverlay;

    int32_t mMsgEnabled;    // camera msg to be handled
    notify_callback mNotifyCallback;
    data_callback mDataCallback;
    data_callback_timestamp mDataCallbackTimestamp;
    void *mCallbackCookie;  // same for all callbacks
    int mDebugFps;
    int kPreviewBufferCountActual;
    int previewWidth, previewHeight;
};

}; // namespace android

#endif
