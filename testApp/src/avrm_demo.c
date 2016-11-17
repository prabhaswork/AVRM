#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "avrm.h"
#include "avrm_prv.h"

//extern tAVRM_handle gAVRM_handle;
void signal_handler(INT32 Signal)
{
	if (Signal == SIGINT || Signal == SIGTERM)
	{
		printf("New Received Signal & Stopping Recoder pipeline\n");
		AVRM_Stop();
		AVRM_DeInit();
		//pthread_join(gAVRM_handle->filewrite_thread, NULL);
		usleep(100);
		exit(0);        
	}
}

int main()
{

	INT32 count = 0;
	INT32 loopcnt = 0;
	INT32 numRecSession = 1;
	INT32 width = 0, height = 0;
	INT8 fname[100];
	eAVRM_RETURN playing = AVRM_FAILURE;
	eAVRM_RETURN nRetVal = AVRM_FAILURE;
        struct sigaction sa;
	eAVRM_MODE mode = AVRM_AUDIO_VIDEO;//AVRM_AUDIO_VIDEO or AVRM_VIDEO_ONLY or AVRM_AUDIO_ONLY

        /*Signal handling*/
        sa.sa_handler = &signal_handler;
    	sa.sa_flags = SA_RESTART;
	// Block every signal during the handler
    	sigfillset(&sa.sa_mask);
        if (sigaction(SIGINT, &sa, NULL) == -1)
        {
		printf("Warning!!...Application will not catch Ctrl+c signal\n");
		return 1;                
	}
        if (sigaction(SIGTERM, &sa, NULL) == -1) {
		printf("Warning!!...Application will not catch Ctrl+c signal\n");
		return 1;
	}

	printf("welcome AVRM mode = %d\n",mode);

	printf("\n Set number of recording sessions (loop count)");
	printf("\n ===============================================\n");
	scanf("%d", &numRecSession);

	do	
	{
		loopcnt++;
		nRetVal = AVRM_Init(mode);
		if (nRetVal == AVRM_SUCCESS)
		{
			nRetVal = AVRM_SetVidCaptSource(AVRM_VIDSRC_DVI);
			if((mode == AVRM_AUDIO_VIDEO) || (mode == AVRM_VIDEO_ONLY))
			{
				printf("setting video configuration in avrm demo\n");
				nRetVal = AVRM_SetVidCaptSource(AVRM_VIDSRC_DVI);
				if(nRetVal == AVRM_FAILURE)
				{
					printf("Failed to set video capture source\n");
				}
				//nRetVal = AVRM_SetVidCaptFPS(AVRM_CAPT_FPS_4);
                                //nRetVal = AVRM_SetVidCaptFPS(AVRM_CAPT_FPS_6);
                                //nRetVal = AVRM_SetVidCaptFPS(AVRM_CAPT_FPS_10);
                                //nRetVal = AVRM_SetVidCaptFPS(AVRM_CAPT_FPS_12);
                                //nRetVal = AVRM_SetVidCaptFPS(AVRM_CAPT_FPS_15);
				unsigned int framerate = 0;

				printf("\n Set capture framerate");
				printf("\n ===============================================");
				printf("\n 1. 4 fps");
				printf("\n 2. 6 fps");
				printf("\n 3. 10 fps");
				printf("\n 4. 12 fps");
				printf("\n 5. 15 fps");
				printf("\n 6. 30 fps\n");
				scanf("%d", &framerate);
				switch(framerate)
				{
					case 1:
						nRetVal = AVRM_SetVidCaptFPS(AVRM_CAPT_FPS_4);
						break;
					case 2:
						nRetVal = AVRM_SetVidCaptFPS(AVRM_CAPT_FPS_6);
						break;
					case 3:
						nRetVal = AVRM_SetVidCaptFPS(AVRM_CAPT_FPS_10);
						break;
					case 4:
						nRetVal = AVRM_SetVidCaptFPS(AVRM_CAPT_FPS_12);
						break;
					case 5:
						nRetVal = AVRM_SetVidCaptFPS(AVRM_CAPT_FPS_15);
						break;
					case 6:
						nRetVal = AVRM_SetVidCaptFPS(AVRM_CAPT_FPS_30);
						break;
					default:
						nRetVal = AVRM_SetVidCaptFPS(AVRM_CAPT_FPS_30);
						break;

				}
				if(nRetVal == AVRM_FAILURE)
				{
					printf("Failed to set capture fps\n");
				}
				UINT32 enc = 0;
				printf("\n Select video encoder");
				printf("\n ===============================================");
				printf("\n 1. OMX h264 encoder");
				printf("\n 2. OMX mpeg4 encoder\n");
				scanf("%d", &enc);
				if (enc != 1 && enc != 2)
					enc = 1;
				printf("\n Select encoding profile");
				printf("\n ===============================================");
				UINT32 bitrate = 0, enc_profile=0,bit =0;
				if (enc == 1){
					printf("\n 1. Base profile");
					printf("\n 2. Main profile");
					printf("\n 3. High profile\n");
					scanf("%d", &enc_profile);
					if (enc_profile == 0 || enc_profile > 3)
						enc_profile = 1;
						
					/*printf("\n Set encoding bitrate (1000 - 10000 Kbps)");
					printf("\n ===============================================\n");
					printf("\n 1. 1000 Kbps");
					printf("\n 2. 2000 Kbps");
					printf("\n 3. 10000 Kbps\n");
					 
					scanf("%d", &bit);	
					if (bit == 1)
						bitrate = 1000000;
					else if (bit == 2)
						bitrate = 2000000;
					else if (bit == 3)
						bitrate = 10000000;
					else
						bitrate = 2000000;*/

					printf("\n Set encoding bitrate (500 - 10000 Kbps)");
					printf("\n ===============================================\n");
					scanf("%d", &bit);	
					if (bit < 500 || bit > 10000){
						printf("\n Bitrate not in the range  500-10000 Kbps. Setting to 2000 Kbps");
						bitrate = 2000000;
					}
					bitrate = bit*1000;
				
					if (enc_profile == 1)	
						nRetVal = AVRM_SetVidEncConfig(AVRM_VID_ENC_H264, AVRM_H264_BL, bitrate); 
					else if (enc_profile == 2)	
						nRetVal = AVRM_SetVidEncConfig(AVRM_VID_ENC_H264, AVRM_H264_ML, bitrate); 
					else
						nRetVal = AVRM_SetVidEncConfig(AVRM_VID_ENC_H264, AVRM_H264_HL, bitrate);

				}
				else{
					printf("\n 1. simple profile\n");
					scanf("%d", &enc_profile);
					if (enc_profile != 1)
						enc_profile = 1;
						
					/*printf("\n Set encoding bitrate (1000000 - 10000000 bps)");
					printf("\n ===============================================");
					printf("\n 1. 1000 Kbps");
					printf("\n 2. 2000 Kbps");
					printf("\n 3. 10000 Kbps\n");
					 
					scanf("%d", &bit);	
					if (bit == 1)
						bitrate = 1000000;
					else if (bit == 2)
						bitrate = 2000000;
					else if (bit == 3)
						bitrate = 10000000;
					else
						bitrate = 2000000;*/

					printf("\n Set encoding bitrate (500 - 10000 Kbps)");
					printf("\n ===============================================\n");
					scanf("%d", &bit);	
					if (bit < 500 || bit > 10000){
						printf("\n Bitrate not in the range  500-10000 Kbps. Setting to 2000 Kbps");
						bitrate = 2000000;
					}
					bitrate = bit*1000;
					nRetVal = AVRM_SetVidEncConfig(AVRM_VID_ENC_MPEG4, AVRM_MPEG4_SIMPLE, bitrate);
				}
				if(nRetVal == AVRM_FAILURE)
				{
					printf("Failed to set required encoder configuration\n");
				}
				UINT32 res = 0;
				printf("\n Set video resolution");
				printf("\n ===============================================");
				printf("\n 1. 640 x 480");
				printf("\n 2. 800 x 600");
				printf("\n 3. 848 x 480");
				printf("\n 4. 1024 x 768");
				printf("\n 5. 1280 x 768");
				printf("\n 6. 1280 x 800");
				printf("\n 7. 1280 x 960");
				printf("\n 8. 1280 x 1024");
				printf("\n 9. 1360 x 768");
				printf("\n 10. 1400 x 1050");
				printf("\n 11. 1440 x 900");
				printf("\n 12. 1600 x 900");
				printf("\n 13. 1600 x 1200");
				printf("\n 14. 1680 x 1050");
				printf("\n 15. 1920 x 1080");
				printf("\n 16. 1920 x 1200\n");
				scanf("%d", &res);
				if(res < 1 || res > 16)
					res = 15;
				nRetVal = AVRM_SetVidCaptResolution (res);
				//nRetVal = AVRM_SetVidCaptResolution (AVRM_RES_640_480);
				//nRetVal = AVRM_SetVidCaptResolution (AVRM_RES_800_600);
				//nRetVal = AVRM_SetVidCaptResolution (AVRM_RES_848_480);
				//nRetVal = AVRM_SetVidCaptResolution (AVRM_RES_1024_768);
				//nRetVal = AVRM_SetVidCaptResolution (AVRM_RES_1280_768);
				//nRetVal = AVRM_SetVidCaptResolution (AVRM_RES_1280_800);
				//nRetVal = AVRM_SetVidCaptResolution (AVRM_RES_1280_960);
				//nRetVal = AVRM_SetVidCaptResolution (AVRM_RES_1280_1024);
				//nRetVal = AVRM_SetVidCaptResolution (AVRM_RES_1360_768);
				//nRetVal = AVRM_SetVidCaptResolution (AVRM_RES_1400_1050);
				//nRetVal = AVRM_SetVidCaptResolution (AVRM_RES_1440_900);
				//nRetVal = AVRM_SetVidCaptResolution (AVRM_RES_1600_900);
				//nRetVal = AVRM_SetVidCaptResolution (AVRM_RES_1600_1200);
				//nRetVal = AVRM_SetVidCaptResolution (AVRM_RES_1680_1050);
				//nRetVal = AVRM_SetVidCaptResolution (AVRM_RES_1920_1080);
				//nRetVal = AVRM_SetVidCaptResolution (AVRM_RES_1920_1200);
				if(nRetVal == AVRM_FAILURE)
				{
					printf("Failed to set capture resolution\n");
				}
			}
			if((mode == AVRM_AUDIO_ONLY) || (mode == AVRM_AUDIO_VIDEO))
			{
				nRetVal = AVRM_SetAudioCh(2);
				if(nRetVal == AVRM_FAILURE)
				{
					printf("Failed to set no of audio channels\n");
				}
			}
			UINT32 duration = 0;	
			UINT32 file_size = 0;	
			UINT32 mode = 0;
			unsigned char exit = 0;
			printf("\n Set file split mode");
			printf("\n ===============================================");
			printf("\n 1. Based on file duration");
			printf("\n 2. Based on file size\n");
			scanf("%d",&mode);
			if (mode == 0 || mode >2)
				mode =1;
			if (mode == 1)
			{
				printf("\n Set file duration to split in seconds");
				printf("\n ===============================================\n");
				scanf("%d", &duration);
				exit = 0;
				nRetVal = AVRM_SetCaptFileDuration(duration);
				if(nRetVal == AVRM_FAILURE)
				{
					printf("Failed to set required file duration\n");
				}
			}
			else if (mode == 2){
				printf("\n Set file size to split in MB");
				printf("\n ===============================================\n");
				scanf("%d", &file_size);
				nRetVal = AVRM_SetCaptFileSize(file_size);
				if(nRetVal == AVRM_FAILURE)
				{
					printf("Failed to set required file size\n");
				}
			}
			printf("\n Set total recording duration");
			printf("\n ===============================================\n");
			scanf("%d", &duration);
			nRetVal = AVRM_SetTotalCaptDuration(duration);
			if(nRetVal == AVRM_FAILURE)
			{
				printf("Failed to set total recording duration\n");
			}
			nRetVal = AVRM_SelectFileLocation("../../vru_25jul15");
			if(nRetVal == AVRM_FAILURE)
			{
				printf("Failed to set required file path\n");
			}

			nRetVal = AVRM_Start();
			if(nRetVal == AVRM_FAILURE)
			{
				printf("Failed to start AVRM\n");
			}

			nRetVal = AVRM_GetCreatedFileName(fname);
			if(nRetVal == AVRM_FAILURE)
			{
				printf("Failed to get created file name\n");
				printf("Stopping \n");
				nRetVal = AVRM_Stop();
				if(nRetVal == AVRM_FAILURE)
				{
					printf("Failed to stop AVRM\n");
				}
				printf("De initing\n");
				nRetVal = AVRM_DeInit();
				if(nRetVal == AVRM_FAILURE)
				{
					printf("Failed to deinit AVRM\n");
				}
                                goto exit;
			}
			else
			{
				printf("created file name is %s \n",fname);
			}

			while(1)
			{
				playing = AVRM_Status();
				if (playing == AVRM_SUCCESS){
					//printf("\nSuccess");
					//usleep(100);   
					sleep(1);   
				} 
				else if (playing == AVRM_RESMISMATCH)
				{
					printf("Res mis match occur\n");
					AVRM_GetVidCaptResolution(&width, &height);
					printf("width and height from driver %d  %d \n",width, height);
					break;
				}
				else
				{
					break;
				}
			}
			printf("Stopping \n");
			nRetVal = AVRM_Stop();
			if(nRetVal == AVRM_FAILURE)
			{
				printf("Failed to stop AVRM\n");
			}
			printf("De initing\n");
			nRetVal = AVRM_DeInit();
			if(nRetVal == AVRM_FAILURE)
			{
				printf("Failed to deinit AVRM\n");
			}
exit:
		 printf("done\n");
		}
		else
		{
			printf("AVRM_Init failed\n");
			break;
		}

#if 0
		nRetVal = AVRM_Init(mode);
		if (nRetVal == AVRM_SUCCESS)
		{
			AVRM_SetVidCaptSource(AVRM_VIDSRC_DVI);
			if(mode != AVRM_AUDIO_ONLY)
			{
				printf("setting video configuration in avrm demo\n");
				//AVRM_SetVidEncConfig(AVRM_VID_ENC_MPEG4, AVRM_MPEG4_SIMPLE, 600000);
				AVRM_SetVidEncConfig(AVRM_VID_ENC_H264, AVRM_H264_ML, 2000000);
				AVRM_SetVidCaptResolution (AVRM_RES_1920_1080);
			}
			if(mode != AVRM_VIDEO_ONLY)
			{
				printf("setting audio configuration in avrm demo\n");
				AVRM_SetAudioCh(1);
			}
			//printf("setting file duration in sec\n");
			//AVRM_SetCaptFileDuration(30);
			printf("setting file size in mb\n");
			AVRM_SetCaptFileSize(10);
			printf("Setting file path\n");
			AVRM_SelectFileLocation("/home/root/");
			printf("Starting pipeline\n");
			nRetVal = AVRM_Start();

			while((playing = AVRM_Status()) == AVRM_SUCCESS)
			{
				sleep(1);
			}
			printf("Stopping \n");
			AVRM_Stop();
			printf("De initing\n");
			AVRM_DeInit();

		}
		else
		{
			printf("AVRM_Init failed\n");
			break;
		}

#endif
	}while(loopcnt < numRecSession);

	printf("Ending Demo\n");
}
