#ifndef AVRM_H
#define AVRM_H
typedef int INT32;
typedef unsigned int UINT32;
typedef char INT8;
/* ENUMS */

/* avrm mode */
typedef enum
{
    AVRM_VIDEO_ONLY = 0,
    AVRM_AUDIO_ONLY,
    AVRM_AUDIO_VIDEO
}eAVRM_MODE;

typedef enum
{
    AVRM_VIDSRC_FPGA = 0,
    AVRM_VIDSRC_DVI,
    AVRM_VIDSRC_ERR = -1
}eAVRM_VIDSRC;

/* Enums used to describe Video Resolutions. */

typedef enum
{
    AVRM_RES_MIN = 0,
    AVRM_RES_640_480,
    AVRM_RES_800_600,
    AVRM_RES_848_480,
    AVRM_RES_1024_768,
    AVRM_RES_1280_768,
    AVRM_RES_1280_800,
    AVRM_RES_1280_960,
    AVRM_RES_1280_1024,
    AVRM_RES_1360_768,
    AVRM_RES_1400_1050,
    AVRM_RES_1440_900,
    AVRM_RES_1600_900,
    AVRM_RES_1600_1200,
    AVRM_RES_1680_1050,
    AVRM_RES_1920_1080,
    AVRM_RES_1920_1200,
    AVRM_RES_MAX

}eAVRM_RESOLUTION;
    
typedef enum
{
    AVRM_CAPT_FPS_ERR = -1,
    AVRM_CAPT_FPS_4 = 0,
    AVRM_CAPT_FPS_6,
    AVRM_CAPT_FPS_10,
    AVRM_CAPT_FPS_12,
    AVRM_CAPT_FPS_15,
    AVRM_CAPT_FPS_30
}eAVRM_CAPT_FPS;
    


/* Enum to represent capture output formats */

typedef enum
{
    AVRM_CAPT_OUT_FMT_MIN = 0,
    AVRM_CAPT_OUT_FMT_YUV_420,
    AVRM_CAPT_OUT_FMT_MAX

}eAVRM_CAPT_OUT_FMT;


/* Enum to specify Video Encoder type */

typedef enum
{
    AVRM_VID_ENC_MIN = 0,
    AVRM_VID_ENC_H264,
    AVRM_VID_ENC_MPEG4,
    AVRM_VID_ENC_MAX

}eAVRM_VID_ENC_TYPE;


/* Video Encoder profiles */

typedef enum
{
    AVRM_H264_BL = 1,
    AVRM_H264_ML = 2,
    AVRM_H264_HL = 8,
    AVRM_MPEG4_SIMPLE = 1,
    AVRM_VIDENC_PROF_ERR = -1

}eAVRM_VIDENC_PROF;


/* Return values from API calls */

typedef enum
{
    AVRM_SUCCESS = 0,
    AVRM_RESMISMATCH = 1,
    AVRM_FAILURE = -1,
    AVRM_INVALID_PARAM = -2

}eAVRM_RETURN;

eAVRM_RETURN AVRM_Status();
eAVRM_RETURN AVRM_Init(eAVRM_MODE avrm_mode);
eAVRM_RETURN AVRM_SetVidCaptSource ( eAVRM_VIDSRC eVidSrc);
eAVRM_RETURN AVRM_GetVidCaptSource ( eAVRM_VIDSRC *eVidSrc);
eAVRM_RETURN AVRM_SetVidCaptResolution ( eAVRM_RESOLUTION eResolution);
eAVRM_RETURN AVRM_SetAudioCh(UINT32 channel);
eAVRM_RETURN AVRM_SetCaptFileSize(UINT32 size_in_mb);
eAVRM_RETURN AVRM_GetVidCaptResolution ( INT32 *width, INT32 *height);
eAVRM_RETURN AVRM_ClearVidCaptResolution ();
eAVRM_RETURN AVRM_SetVidCaptFPS(eAVRM_CAPT_FPS fps);
eAVRM_RETURN AVRM_GetVidCaptFPS(eAVRM_CAPT_FPS *fps);
eAVRM_RETURN AVRM_SetVidEncConfig( eAVRM_VID_ENC_TYPE enc, eAVRM_VIDENC_PROF profile, INT32 bitrate);
eAVRM_RETURN AVRM_GetVidEncConfig(eAVRM_VID_ENC_TYPE *enc, eAVRM_VIDENC_PROF *profile, INT32 *bitrate);
eAVRM_RETURN AVRM_ClearVidEncConfig();
eAVRM_RETURN AVRM_SelectFileLocation(INT8* loc);
eAVRM_RETURN AVRM_SetCaptFileDuration(UINT32 seconds);
eAVRM_RETURN AVRM_SetTotalCaptDuration(UINT32 duration);
eAVRM_RETURN AVRM_GetCreatedFileName(INT8 *fname);
eAVRM_RETURN AVRM_Start();
eAVRM_RETURN AVRM_Stop();
eAVRM_RETURN AVRM_DeInit();
#endif
