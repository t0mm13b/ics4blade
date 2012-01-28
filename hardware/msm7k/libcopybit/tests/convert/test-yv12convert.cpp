#include <stdio.h>
#include <utils/RefBase.h>
#include <ui/GraphicBuffer.h>
#include <utils/Timers.h>
#include <cutils/log.h>
#include "gralloc_priv.h"
#include "software_converter.h"

void printbytes( char *p, int n, int stride)
{
    for(int i = 0 ; i < n; i++) {
        printf("%02x",p[i]);
        if(!((i+1) % stride))
            printf("\n");
    }
    printf("\n");
}

int main(int argc, char ** argv)
{
    copybit_image_t c;
    private_handle_t *hnd;

    // Test functionality on byte pattern
    int w = 34;
    int h = 34;
    int stride = ALIGN(w, 16);
    int c_w = ALIGN(stride/2, 16);
    int y_size = stride *h;
    int c_size = c_w * h/2;

    android::sp<android::GraphicBuffer> bytepattern;
    bytepattern = new android::GraphicBuffer(stride, h, HAL_PIXEL_FORMAT_YV12,
                            GRALLOC_USAGE_PRIVATE_PMEM_ADSP | GRALLOC_USAGE_HW_2D);
    c.w = stride;
    c.h = h;
    c.horiz_padding = stride - w;
    c.format = HAL_PIXEL_FORMAT_YV12;
    c.handle = (native_handle_t *) bytepattern->getNativeBuffer()->handle;

    hnd = (private_handle_t *) c.handle;

    // Color the image with an easily recognizable byte pattern
    memset((void*)hnd->base, 0x11, stride*h);
    for(int i = w; i < stride*h ; i += stride) {
        memset((void*)(hnd->base + i), 0x00, stride - w);
    }
    memset((void*)(hnd->base + y_size), 0xCC, c_size);
    memset((void*)(hnd->base + y_size + c_size), 0xBB, c_size);
    for(int i = w/2; i < c_w*h ; i += c_w) {
        memset((void*)(hnd->base + y_size +i), 0x00, c_w - w/2);
    }
    memset((void*)(hnd->base + y_size), 0x55,  w/2);
    memset((void*)(hnd->base + y_size+c_size*2 - c_w), 0x44,  w/2);
    //Prints the chroma component
    printbytes((char*)hnd->base + y_size, c_size * 2, c_w);
    convertYV12toYCrCb420SP(&c);
    printbytes((char*)hnd->base + y_size, c_size * 2, c_w);


    // Test performance on a real image
    // Please copy a test YV12 image to /data/copybit_tests/yv12image.raw
    // and set it's dimensions here before running this test
    w = 640;
    h = 360;
    stride = ALIGN(w, 16);
    c_w = ALIGN(stride/2, 16);
    y_size = stride *h;
    c_size = c_w * h/2;

    android::sp<android::GraphicBuffer> yv12image;
    yv12image = new android::GraphicBuffer(w, h, HAL_PIXEL_FORMAT_YV12,
                            GRALLOC_USAGE_PRIVATE_PMEM_ADSP | GRALLOC_USAGE_HW_2D);
    c.w = stride;
    c.h = h;
    c.horiz_padding = stride - w;
    c.format = HAL_PIXEL_FORMAT_YV12;
    c.handle = (native_handle_t *) yv12image->getNativeBuffer()->handle;
    hnd = (private_handle_t *) c.handle;
    FILE * fp = fopen("/data/copybit_tests/yv12image.raw", "r");
    fread((void*)hnd->base, stride*h*3/2, 1, fp);
    fclose(fp);
    nsecs_t before = systemTime();
    convertYV12toYCrCb420SP(&c);
    nsecs_t after = systemTime();
    printf("\nTime taken to convert: %ld microseconds\n", long(ns2us(after - before)));

    fp = fopen("/data/copybit_tests/yuv420spimage-640x360.raw", "w");
    fwrite((void*)hnd->base, y_size + c_size*2, 1, fp);
    fclose(fp);

    return 0;
}
