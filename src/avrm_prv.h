#ifndef AVRM_PRV_H
#define AVRM_PRV_H
#include <pthread.h>
#include <gst/gst.h>
#include "avrm.h"

#define DEFAULT_CAPT_MODE "720p"
#define FPGA_DVI_GPIO 46
#define FPGA_DVI_GPIO_VAL "/sys/class/gpio/gpio46/value"



/* Global AVRM handle structure used internally in the module. */


typedef struct
{
    INT32 nDuration;
    INT32 nFsize_in_mb;
    INT32 nWidth;
    INT32 nHeight;
    gboolean bResMisMatch;
    gboolean gbEosInitiated;
    gboolean bFailedToGoPlayState;
    eAVRM_MODE avrm_mode;
    eAVRM_VID_ENC_TYPE encType;
    INT8 cFilePath[100];
    GMainLoop *main_loop;
    GMainContext *main_loop_cntxt;
    pthread_t playing_thread;
    GstElement *pipeline;
    GstElement *omx_videosrc;
    GstElement *audio_capsfilter;
    GstElement *clockoverlay;
    GstElement *alsaAudSrc;
    GstElement *audioQue;
    GstElement *audioRate;
    GstElement *audioPerf;
    GstElement *videoPerf;
    GstElement *omx_videoEnc;
    GstElement *AVIMux;
    GstElement *FileSink;

    pthread_t filewrite_thread;
    INT8 cFile[100];
    INT8 fname[30];
    GstElement *appsink;
    GstElement *videoqueue;

	
}tAVRM_HandleStr;

typedef struct
{
    unsigned int fourCC;
    unsigned int flags;
    unsigned int chunkOffset;
    unsigned int chunkLength;
}AVRM_AVI_Index_t;

typedef enum
{
    AVRM_CHUNK_VIDEO,
    AVRM_CHUNK_AUDIO,
    AVRM_CHUNK_INVALID = 0x1fffffff
}AVRM_ChunkType_t;

typedef tAVRM_HandleStr *tAVRM_handle;
#endif
