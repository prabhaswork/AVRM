#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/prctl.h>



#include "avrm_prv.h"

#define AVIIF_KEYFRAME  0x00000010L // this frame is a key frame.
#define AVRM_RIFF_AVIH_HASINDEX 0x00000010L 
#define AVRM_RIFF_AVIH_ISINTERLEAVED 0x00000100L

static unsigned char framerate = 0;


tAVRM_handle gAVRM_handle =NULL;
eAVRM_RETURN ismainloop_running= AVRM_FAILURE;
static void *play_pipeline (void); 
static void *set_timestamp (void); 
static void Video_Res_mismatch(void);
static unsigned char eos = 0;
static unsigned char file_closed = 0;
static pthread_attr_t detach_attr;



GstElement *audioQ;
static GstPad *videoqueue_src_pad, *avimux_video_sink_pad;
static GstPad *audioqueue_src_pad, *avimux_audio_sink_pad;

#define RATE_CONTROL	1
#define RING_BUFFER
#define FILESPLIT_APPSINK
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
static guint64 NANOTOMILLISECS = ( 1000 * 1000);
#ifdef FILESPLIT_APPSINK
#include "avrmringbuffer.h"
#include <semaphore.h>
static void *file_write_thread(void); 
static GstFlowReturn onNewPrerollFromSink(GstElement *elt, int *data);
static GstFlowReturn onNewBufferFromAppSink(GstElement *elt, int *data);
static RbufHandle_t ringBufHandle;
static sem_t ringBufReady;
#define NUM_BUFFERS 15000
static char thread_exit = 0;
static unsigned int file_duration = 0;
static unsigned int total_recording_duration = 0;
static pthread_t thread_id;
//#define OMX_BUFFER
static GstElement *buffer_alloc = NULL;
static GstElement *videorate = NULL;

unsigned int audio_packets_count_in_pull_thread = 0;

#define FILE_BLOCKSIZE		(4194304)//(1048576) //(262144) //32768 //(65536)
#define FILE_WRITE_BLOCK

typedef struct
{
	unsigned char data[FILE_BLOCKSIZE];
	unsigned int offset;	
}AVRM_filedatablock_t;

AVRM_filedatablock_t dataBlock;

#define MAX 50
unsigned int queue[MAX] = {0};
int front=-1, rear=-1;
unsigned char exit_after_recording_duration = 0;

unsigned int numVidFrames = 0;
guint64 numAudSamples = 0;


void insert_element(unsigned int num)
{
	if(front==0 && rear==MAX-1)
		printf("\n Queue OverFlow Occured");
	else if(front==-1&&rear==-1)
	{
		front=rear=0;
		queue[rear]=num;

	}
	else if(rear==MAX-1 && front!=0)
	{
		rear=0;
		queue[rear]=num;
	}
	else
	{
		rear++;
		queue[rear]=num;
	}
	//printf("\n Q inserted element %d", num);
}


void get_element(unsigned int *elm)
{
	if(front==-1)
	{
		printf("\n Underflow");
	}
	*elm = queue[front];
	if(front==rear)
		front=rear=-1;
	else
	{
		if(front==MAX-1)
			front=0;
		else
			front++;
		//printf("\n Queue read element is: %d",*elm);
	}

}

static unsigned int AVRM_filewrite(unsigned char *data, unsigned int len, unsigned char force, FILE *fp)
{
	unsigned char *pdata = data;
	unsigned int datalen = len;
	unsigned int loop = 1;

	if(force == 1)
	{
		if(dataBlock.offset > 0)
		{
			fwrite((char*)dataBlock.data, 1, dataBlock.offset, fp);	
			dataBlock.offset = 0;
		}

		fwrite((char*)data, 1, len, fp);	
		fflush(fp);
	}
	else
	{

		do
		{
			unsigned int copylen = 0;
			unsigned int remaining = 0;
			unsigned char *blockptr = NULL;

			copylen = ( (dataBlock.offset + datalen) > FILE_BLOCKSIZE)? (FILE_BLOCKSIZE-dataBlock.offset):datalen;
			remaining = datalen - copylen;	

			blockptr = ((unsigned char*)&(dataBlock.data[0]))+ dataBlock.offset;	

			memcpy(blockptr, pdata, copylen);

			dataBlock.offset += copylen;

			if(dataBlock.offset > FILE_BLOCKSIZE)
			{
				printf("Filewrite Invalid offset !!\n");
			}
			else if(dataBlock.offset == FILE_BLOCKSIZE)
			{
				fwrite((char*)dataBlock.data, 1, dataBlock.offset, fp);	
				fflush(fp);
				dataBlock.offset = 0;	
			}			
			else
			{
				//do nothing
			}

			if(remaining > 0)
			{
				pdata += copylen;
				datalen = datalen - copylen;
			}
			else
			{
				loop = 0;
			}
		}while(loop != 0);
	}

	return len;
}

static unsigned char  AVRM_Is_Iframe(char *data, eAVRM_VID_ENC_TYPE encType);

static unsigned char  AVRM_Is_Iframe(char *data, eAVRM_VID_ENC_TYPE encType)
{
	char *temp = NULL;
	unsigned short dig = 0;
	unsigned int num = 0;
	unsigned char iframe = 0;
	unsigned char byte = 0;
	unsigned char mask = 0xFF;

	if (encType == AVRM_VID_ENC_H264)
	{
		temp = data+4;
		memcpy(&dig, temp, 2);	
		//printf("\n dig is  0x%hu",dig);
		if ( (dig == 0x4227) || (dig == 0x4D27) || (dig == 0x6427) )
                { //This video frame is I-frame
			//printf("\n H264 I_frame");
			iframe = 1;
		}
	}
	else
	{
		temp = data;
		memcpy(&num, temp,4);	
		//printf("\n num is  0x%u",num);
		if ((num == 0xB0010000) || (num == 0xB6010000)){ //This video frame is I-frame
			//Then take first two bits of next byte
			//Check that two bit is 00. Then it is an i-frame
			memcpy(&byte, temp+4, 1);
			//printf("\n Byte value %d",byte);
			mask = 0xC0;
			byte = (byte & mask) >> 6;
			//printf("\n Byte value after masking %d",byte);
			if (byte == 0){
				//printf("\n MPEG4 I_frame");
				iframe = 1;
			}
		}
	}
	return iframe;

}

static void *appsink_pull_thread(void);
static void *appsink_pull_thread(void)
{
	GstBuffer *buffer = NULL, *buffer1 =NULL;
	GstBuffer *header;
    GstBuffer *payload;
	guint8 *headerdata1;
	guint8 *buffer1data;
    guint8* payloaddata;
    guint32 payloadsize = 0;

	gsize sizeBuf = 0;
	static unsigned int totalSize = 0;
	eAVRM_RETURN ret = AVRM_SUCCESS;
	struct timespec tp;
	guint64 time_in_nano_seconds = 0;

#ifdef RATE_CONTROL
	GstClockTime audioStartGstTime = 0;
	GstClockTime currentGstTime = 0;
	unsigned int audioTimeFlag = 0;
	gint32 numAudBlocksToDrop = 0;


	GstClockTime videoStartGstTime = 0;
	unsigned int videoTimeFlag = 0;
#endif

	FILE *fp;
	char filename[20] = {0};
	char new_file = 1, count = 0;
	guint64 start , end, current;

	unsigned int dwFourCC = 0;

	prctl(PR_SET_NAME,"appsink_pull_thread",0,0,0);

	printf("appsink_pull_thread entry\n");
	printf("added audio timestamp tests - bulk print with difference of 1000\n");


	guint64 max_audio_timestamp = 0;
	guint64 current_audio_timestamp = 0;
	guint64 previous_audio_timestamp = 0;

	guint64 max_video_timestamp = 0;
	guint64 current_video_timestamp = 0;
	guint64 previous_video_timestamp = 0;
	int counter_till_iframe = 0;

	int act = 0;
	int topmost_header_recieved = 0;
	int header_recieved = 0;

	unsigned char fps = framerate;
	if(framerate % 2 != 0){
		fps++;
	}

    //appsinkthread_exit = 0;
	while(1)
	{
		//printf(" Before gst_app_sink_pull_buffer\n");
		buffer1 = gst_app_sink_pull_buffer((GstAppSink*)gAVRM_handle->appsink);
		
		if (buffer1 != NULL)
		{

			buffer1data = GST_BUFFER_DATA (buffer1);

			if(buffer1data == NULL)
			{
				printf("buffer1data is NULL\n");
				gst_buffer_unref(buffer1);
				continue;
			}

			/* Code to split data pulled into header and payload  [BEGIN]*/

			if(! topmost_header_recieved){
				// code to recieve very first header
				header = gst_buffer_copy(buffer1);
				gst_buffer_copy_metadata(header, buffer1,GST_BUFFER_COPY_ALL);
				gst_buffer_unref(buffer1);
			}else{

				// to cut header data
				header = gst_buffer_new_and_alloc (8);
				headerdata1 = GST_BUFFER_DATA (header);
				memcpy(headerdata1, buffer1data , 8);

				// crosschecks for audio data
				unsigned int *ptr = (unsigned int *)headerdata1;
				dwFourCC = (unsigned int)(*ptr);
				if(dwFourCC == 0x62773130) // if its audio header
				{
#ifdef RATE_CONTROL
					if(audioTimeFlag==0)
					{
						//start_time_in_nano_seconds = time_in_nano_seconds;
						audioStartGstTime = gst_util_get_timestamp();
						audioTimeFlag = 1;
					}
					else
					{
						currentGstTime = gst_util_get_timestamp();
					}

#if 0					
					if(numAudBlocksToDrop > 0)
					{
						gst_buffer_unref(buffer1);
						gst_buffer_unref(header);
						numAudBlocksToDrop--;
						continue;
					}	
#endif				

					guint64 timeSoFar = (currentGstTime - audioStartGstTime)/NANOTOMILLISECS;

					guint64 timeSoFarSecs = timeSoFar/1000;
					guint64 remmsec	= timeSoFar%1000;
					guint64 audOffset = (12 * (4096/4));
					//guint64  expSamples = (timeSoFarSecs * 44100) + ((44100 * remmsec)/1000)+ audOffset ;
					guint64  expSamples = (timeSoFarSecs * 48000) + ((48000 * remmsec)/1000)+ audOffset ;

					//gint64 audsamplediff = expSamples - numAudSamples;
					gint64 audsamplediff = numAudSamples - expSamples;

					//guint64 diff = (previousAudio!=0)?((currentTime - previousAudio)/NANOTOMILLISECS):previousAudio;
					//if(diff > 100)
					//{
					
					//}

					if(audsamplediff > (4096)) 	//atleast 4 blocks are to be dropped
					{
						numAudBlocksToDrop = audsamplediff/1024;

					/*	printf("\naudsamplediff : %"G_GUINT64_FORMAT", numAudBlocksToDrop:  %"G_GUINT64_FORMAT " , time so far:  %"G_GUINT64_FORMAT " \n",
							audsamplediff, numAudBlocksToDrop, timeSoFarSecs);  */

						if(numAudBlocksToDrop > 0)
						{
							gst_buffer_unref(buffer1);
							gst_buffer_unref(header);
							continue;
						}
					}
					
					numAudSamples += (4096/4);

#endif
#if 0 //logs
					current_audio_timestamp = GST_BUFFER_TIMESTAMP(buffer1)/NANOTOMILLISECS;
					if (current_audio_timestamp < max_audio_timestamp) printf("\n *****audio_timestamp_repeat_at %"G_GUINT64_FORMAT " ***** \n\n", current_audio_timestamp);
					if (current_audio_timestamp > max_audio_timestamp) max_audio_timestamp = current_audio_timestamp;
					if (current_audio_timestamp - previous_audio_timestamp > 1000){ printf("\n ================ previous: %"G_GUINT64_FORMAT " current: %"G_GUINT64_FORMAT " ====================== \n\n",previous_audio_timestamp,current_audio_timestamp); act = 1; }
					previous_audio_timestamp = current_audio_timestamp;


					audio_packets_count_in_pull_thread ++; // to compare this packet count with file write thread just to crosscheck whether we miss any packets.
#endif 
				}else if(dwFourCC == 0x62643030){ // if it is video header

					

#ifdef RATE_CONTROL
					if(videoTimeFlag==0)
					{
						//start_time_in_nano_seconds = time_in_nano_seconds;
						videoStartGstTime = gst_util_get_timestamp();
						videoTimeFlag = 1;
					}
					else
					{
						currentGstTime = gst_util_get_timestamp();
					
						guint64 timeSoFar = (currentGstTime - videoStartGstTime)/NANOTOMILLISECS;

						guint64 timeSoFarSecs = timeSoFar/1000;
						guint64 remmsec	= timeSoFar%1000;

						guint64  expSamples = (timeSoFarSecs * fps) + ((fps * remmsec)/1000) ;

						
						gint64 vidsamplediff = numVidFrames - expSamples;

						if(AVRM_Is_Iframe((char*)buffer1data, gAVRM_handle->encType)){
							counter_till_iframe = 0;
						}

						if(vidsamplediff > 5){ 
							
							if(counter_till_iframe >= (fps-1)){
								/*printf("Actual video frames(  %"G_GUINT64_FORMAT " ) exceeded expected video frames (  %"G_GUINT64_FORMAT " ). Time so far:  %"G_GUINT64_FORMAT "  \n\n",numVidFrames,expSamples,timeSoFarSecs );
								printf("skipping extra video frames \n");*/

								gst_buffer_unref(buffer1);
								gst_buffer_unref(header);
								continue;
							}
							
						}

					}

					numVidFrames ++;
					counter_till_iframe ++;

#endif
				}

				// to cut payload data
				payloadsize = GST_BUFFER_SIZE(buffer1) - 8;
				payload = gst_buffer_new_and_alloc (payloadsize);
				payloaddata = GST_BUFFER_DATA (payload);
				memcpy(payloaddata, (buffer1data+8), payloadsize);

				// to copy metadata
				gst_buffer_copy_metadata(header, buffer1,GST_BUFFER_COPY_ALL);
        		gst_buffer_copy_metadata(payload, buffer1,GST_BUFFER_COPY_ALL);

        		clock_gettime(CLOCK_REALTIME, &tp);

        		time_in_nano_seconds = (guint64)tp.tv_sec*1000*1000*1000;
				time_in_nano_seconds += (guint64)tp.tv_nsec;

				GST_BUFFER_TIMESTAMP(header) = time_in_nano_seconds;
				GST_BUFFER_TIMESTAMP(payload) = time_in_nano_seconds;

				//buffer = gst_buffer_copy(buffer1); // these two steps are to keep some of the old codes below the same.
				//GST_BUFFER_TIMESTAMP(buffer) = time_in_nano_seconds;

				gst_buffer_unref(buffer1);

				//printf("\n *****current payload size %"G_GUINT32_FORMAT " ***** \n\n", payloadsize);
			}


			/* Code to split data pulled into header and payload  [END]*/

#if 0
/******* Adding some test prints to test audio repeat problem through repeating timestamps [BEGIN] *****************/

			unsigned int size = GST_BUFFER_SIZE((GstBuffer*)buffer1);
			if (size == 8)
			{
				dwFourCC = 0;

				unsigned int *ptr = GST_BUFFER_DATA((GstBuffer*)buffer1);
				dwFourCC = (unsigned int)(*ptr);
			}else{

				if(dwFourCC == 0x62773130)
				{
					current_audio_timestamp = GST_BUFFER_TIMESTAMP(buffer1)/NANOTOMILLISECS;
					if (current_audio_timestamp < max_audio_timestamp) printf("\n *****audio_timestamp_repeat_at %"G_GUINT64_FORMAT " ***** \n\n", current_audio_timestamp);
					if (current_audio_timestamp > max_audio_timestamp) max_audio_timestamp = current_audio_timestamp;
					if (current_audio_timestamp - previous_audio_timestamp > 1000){ printf("\n ================ previous: %"G_GUINT64_FORMAT " current: %"G_GUINT64_FORMAT " ====================== \n\n",previous_audio_timestamp,current_audio_timestamp); act = 1; }
					previous_audio_timestamp = current_audio_timestamp;
				}

				else if(act == 1)
				{
					printf("\n XXXXXXXXXXXXXXXXX  video timestamp is %"G_GUINT64_FORMAT " *XXXXXXXXXXXXXXXXXX \n\n", GST_BUFFER_TIMESTAMP(buffer1)/NANOTOMILLISECS);
					act = 0;
				}
				else
				{
					//printf("\n ***** current_video_timestamp is %"G_GUINT64_FORMAT " ***** \n\n", GST_BUFFER_TIMESTAMP(buffer1)/NANOTOMILLISECS);
				}

			}




/******* Adding some test prints to test audio repeat problem through repeating timestamps [END] *****************/

#endif

		/*	buffer = gst_buffer_copy(buffer1);
			clock_gettime(CLOCK_REALTIME, &tp);

			//For copying timestamps 
			gst_buffer_copy_metadata(buffer, buffer1,GST_BUFFER_COPY_ALL);
			time_in_nano_seconds = (guint64)tp.tv_sec*1000*1000*1000;
			time_in_nano_seconds += (guint64)tp.tv_nsec;

			GST_BUFFER_TIMESTAMP(buffer) = time_in_nano_seconds;
			gst_buffer_unref(buffer1); */

#ifdef H264_DUMP
			if (new_file)
			{
				start = ( ( GST_BUFFER_TIMESTAMP(buffer) ) / NANOTOMILLISECS);
				end = start + (file_duration *1000);
				sprintf(filename, "dump_%d.h264", count);
				new_file = 0;
				printf("\nStart %" G_GUINT64_FORMAT "End %"G_GUINT64_FORMAT "\n",start,end);

			}
#endif	
			//sizeBuf = GST_BUFFER_SIZE(buffer);
			totalSize += payloadsize+8;

#ifndef H264_DUMP
#ifdef RING_BUFFER
			//ret  = RingBufWrite(ringBufHandle, buffer); // -- modified code below
			if(topmost_header_recieved)
			{
				ret  = RingBufWrite(ringBufHandle, header);
				sem_post(&ringBufReady);

				ret  = RingBufWrite(ringBufHandle, payload); 
				sem_post(&ringBufReady); 
			}
			else
			{

        		clock_gettime(CLOCK_REALTIME, &tp);

        		time_in_nano_seconds = (guint64)tp.tv_sec*1000*1000*1000;
				time_in_nano_seconds += (guint64)tp.tv_nsec;

				GST_BUFFER_TIMESTAMP(header) = time_in_nano_seconds;

				ret  = RingBufWrite(ringBufHandle, header);
				sem_post(&ringBufReady);
				topmost_header_recieved = 1;
			}
			//unsigned int *chunkCode = GST_BUFFER_DATA((GstBuffer*)buffer); // ??
#endif
			//printf("sem_post(&ringBufReady);\n");
				
#endif
#ifdef H264_DUMP
			fp = fopen(filename, "a");
			fwrite(GST_BUFFER_DATA((GstBuffer*)buffer), 1, sizeBuf, fp);
			fclose(fp);

			current = (GST_BUFFER_TIMESTAMP(buffer)/ NANOTOMILLISECS);
				printf("\nCurrent %" G_GUINT64_FORMAT "\n",current);

			if( ((current - start)/1000) >= file_duration){
				printf("\n Spliiting\n");
				count++;
				new_file =1;
			}
			if (eos){
				file_closed = 1;
				break;}
#endif

		}
		else
		{

				if (!(g_main_loop_is_running (gAVRM_handle->main_loop)))
				{
					//printf("\n Main loop not running : appsink pull thread break");
					sem_post(&ringBufReady);	
					break;
				}
				else
					usleep(30000);
		}

	}
	printf("\n Appsink pull thread exiting");
	//appsinkthread_exit = 1;
	pthread_exit(0);
		

}
static void *file_write_thread(void)
{
	unsigned int curBuf = 0;
	unsigned char identify_start_position = 1;
	 guint64  start = 0;
	 guint64  recording_start_time = 0;
	 guint64  current = 0;
	 guint64  end = 0;
	 guint64 temp_timestamp = 0;
	 FILE *fp = NULL;
	 eAVRM_RETURN ret = AVRM_SUCCESS;

	unsigned char fileStart = 1;
	unsigned char fileEnd = 0;
	unsigned char firstFile = 1; 
	unsigned char *headerData = NULL;
	unsigned int headerDatasize = 0;
	unsigned int totalFileSize = 0;
        AVRM_AVI_Index_t *aviIndex = NULL;
	unsigned int numIndex = 256;
	unsigned int indexCount = 0;
	unsigned int dwFourCC = 0;
	unsigned int dwSize= 0;
	unsigned int numVideoFrames = 0;
//	unsigned int iframe_count = 0;
	unsigned int totalChunkSize = 0;
	unsigned int chunkRemainBytes = 0;
	unsigned int dwFourCC_video = 0;

	unsigned char chunkStart = 0;
	unsigned char iframe = 0;
	unsigned char *iframe_data = NULL;
	unsigned int iframe_size = 0;
	unsigned char chunkHeader[8] = {0};

	unsigned char recording_duration_end = 0;
	struct stat buf;
	gint fret = -1;
	unsigned int audio_data_count = 0, audio_header_count = 0;
	unsigned int audio_data = 0, elm =0;

	prctl(PR_SET_NAME,"filewrite_thread",0,0,0);

	printf("file_write_thread entry\n");

	while(1)
	{
		//printf(" Before RingBufRead\n");
		sem_wait(&ringBufReady);
#ifdef RING_BUFFER
		curBuf = 0;
		 ret = RingBufRead(ringBufHandle, &curBuf);
#endif
		if (curBuf != 0)
		{
#if 0
			unsigned int *ptr = GST_BUFFER_DATA((GstBuffer*)curBuf);
			unsigned int size = GST_BUFFER_SIZE((GstBuffer*)curBuf);
			if (size == 8){
				dwFourCC = 0;
				dwSize= 0;
				dwFourCC = (unsigned int)(*ptr);
				//if(dwFourCC != 0x62643030) //audio data
				if(dwFourCC == 0x62773130) //audio data
				{
					dwFourCC = 0;
					audio_data = 1;	
					//printf("\n Next Audio data");
					if (audio_header_count > 22)
					{
						//printf("\n Inserting audio header %d after buffer 22", ptr);
						//insert_element((unsigned int)ptr);
						insert_element(curBuf);
						/**Store current timestamp*/
						temp_timestamp = GST_BUFFER_TIMESTAMP((GstBuffer*)curBuf);
						elm = 0;
						get_element(&elm);
						//printf("\n Got audio header from queue%d",elm);
						curBuf = elm;		
						/**Assign current timestamp to buffer from queue, This is done as audio data from queue contains 
						   timestamp, which is  22 audio buffers back(as we are queueing 22 audio buffers to acheive AV sync)*/	
						GST_BUFFER_TIMESTAMP((GstBuffer*)curBuf) = temp_timestamp;
						//printf("\n Audio header : Current Timestamp  %"G_GUINT64_FORMAT "\n", temp_timestamp);

					}
					else
					{
						audio_header_count++;
						//printf("\n Inserting audio header %d ", ptr);
						//insert_element((unsigned int)ptr);
						insert_element(curBuf);
						continue;
					}
				}
			}
			else{
				if (audio_data == 1)
				{
					if (size > 1)
					{
						audio_data = 0;
						if (audio_data_count > 22)
						{
							audio_data_count++;
							//printf("\n Inserting audio data %d after buffer 22", ptr);
							insert_element(curBuf);
							/**Store current timestamp*/
							temp_timestamp = GST_BUFFER_TIMESTAMP((GstBuffer*)curBuf);
							elm = 0;
							get_element(&elm);
							//printf("\n Got audio data from queue%d",elm);
							curBuf = elm;			
						/**Assign current timestamp to buffer from queue, This is done as audio data from queue contains 
						   timestamp, which is  22 audio buffers back(as we are queueing 22 audio buffers to acheive AV sync)*/	
							GST_BUFFER_TIMESTAMP((GstBuffer*)curBuf) = temp_timestamp;
						//printf("\n Audio data : Current Timestamp  %"G_GUINT64_FORMAT "\n", temp_timestamp);
							
						}
						else
						{
							audio_data_count++;
							//printf("\n Inserting audio data %d ", ptr);
							insert_element(curBuf);
							continue;
						}
					}		

				}

			}
#endif		
			
			if (identify_start_position)
			{
				identify_start_position = 0;
				start = (GST_BUFFER_TIMESTAMP((GstBuffer*)curBuf)) / NANOTOMILLISECS;
				if ((total_recording_duration != 0) && (recording_start_time == 0) )
				{
					recording_start_time = start;
				}
				end = start + (file_duration*1000);
				printf("\nStart %" G_GUINT64_FORMAT "End %"G_GUINT64_FORMAT "\n",start,end);
				printf("\n Total Recording duration %d", total_recording_duration);
				printf("\n Split file duration %d", file_duration);
				setFileName();
				memset(gAVRM_handle->cFile, 0, sizeof(gAVRM_handle->cFile));
				strcpy(gAVRM_handle->cFile, gAVRM_handle->cFilePath);
				strcat(gAVRM_handle->cFile, gAVRM_handle->fname);
				fp = fopen(gAVRM_handle->cFile, "w");
				//printf("\n New Filename %s",gAVRM_handle->cFile);

				fileStart = 1;
				if(aviIndex != NULL)
				{
					free(aviIndex);
					aviIndex = NULL;
				}
				numIndex = 256;
				indexCount = 0;
				totalChunkSize = 0;
				aviIndex = (AVRM_AVI_Index_t*)malloc(sizeof(AVRM_AVI_Index_t) * numIndex);
			}

			//check chunk details
			unsigned char *chunkBuf = GST_BUFFER_DATA((GstBuffer*)curBuf);
			unsigned int chunkBufsize = GST_BUFFER_SIZE((GstBuffer*)curBuf);
			
			//printf("AVI chunk Buf size : %d\n",chunkBufsize);
			//this flag chunkStart need to be set for all chunks except the very first chunk.
			//very first chunk is header data
			if(firstFile == 0)
				chunkStart = 1;

			//file start
			if(fileStart)
			{
				printf("\n\n Starting new file");
				fileStart=0;
				numVideoFrames = 0;
				//iframe_count = 0;
				totalFileSize = 0;
				//first file
				if(firstFile)
				{
					firstFile=0;
					//copy header data from first chunk for use in subsequent files
					headerData = (unsigned char*) malloc(chunkBufsize * sizeof(unsigned char));
					memset(headerData, 0, chunkBufsize);
					memcpy(headerData, chunkBuf, chunkBufsize);
					headerDatasize = chunkBufsize;
					totalFileSize = headerDatasize;
				}
				else
				{
					//write header copied from the very first chunk of first file
#ifdef FILE_WRITE_BLOCK
					AVRM_filewrite(headerData, headerDatasize, 0, fp);
#else
	 				fwrite((char*)headerData, 1, headerDatasize, fp);	
					fflush(fp);
#endif
					totalFileSize = headerDatasize;
					if (iframe_data != NULL)
					{
						//printf("\n Writing I-frame to start of file");
						//memset(chunkHeader, 0, 8);
						if (gAVRM_handle->encType == AVRM_VID_ENC_MPEG4)
						{
							//For MPEG4 encoding , we are taking last i-frame got, to put into next split file.
							//update  size in header as the size will be updated for every frame, in header parsing part
							//We need size of I-frame in header data 
							memset(chunkHeader, 0, 8);
							memcpy(chunkHeader, &dwFourCC_video, 4);
							//printf("\n I -frame header type 0x%d", dwFourCC_video);
							memcpy(chunkHeader+4, &iframe_size, 4);
						}
#ifdef FILE_WRITE_BLOCK
						AVRM_filewrite(chunkHeader, 8, 0, fp);
#else
						fwrite(chunkHeader, 1, 8, fp);
						fflush(fp);
#endif
#ifdef FILE_WRITE_BLOCK
						AVRM_filewrite(iframe_data, iframe_size, 0, fp);
#else
						fwrite(iframe_data, 1, iframe_size,fp);
						fflush(fp);
#endif
						free(iframe_data);
						iframe_data = NULL;
						totalFileSize += (8 + iframe_size);  //Chunk header size + last iframe size
					
						//Add iframe data to avi index
						unsigned int *temp = NULL;
						temp = (unsigned int*)chunkHeader;
						dwFourCC = 0;
						dwSize = 0;
						dwFourCC = (unsigned int)(*temp);
						temp++;
						dwSize = (unsigned int)(*temp);
						//printf("\nI-frame size got from from stored header %d", dwSize);
						aviIndex[indexCount].fourCC = dwFourCC;
						
						if(dwFourCC == 0x62643030)
						{
							aviIndex[indexCount].flags = 0x12;//AVIIF_KEYFRAME;
							//Check if I-frame
						}
						else
						{
							aviIndex[indexCount].flags = 0;   //TBD
						}

						//aviIndex[indexCount].flags = AVIIF_KEYFRAME;
						//substract chunk header and iframe_size as it is added to total file size above
						aviIndex[indexCount].chunkOffset = (totalFileSize-(8 + iframe_size));
 						//aviIndex[indexCount].chunkLength = dwSize;
 						aviIndex[indexCount].chunkLength = iframe_size;
						indexCount++;
						numVideoFrames++;
						totalChunkSize = (8 + iframe_size);	
						//printf("\n Total chunk size at start of seocnd file %d", totalChunkSize);
					}
				}
			}

			if(1 == chunkStart)
			{
				unsigned int *chunkPtr = (unsigned int*)chunkBuf;
				unsigned int chunkLen = 0;
			        AVRM_ChunkType_t chunkType = AVRM_CHUNK_INVALID;	
                                		
				//check if chunk size is 8 bytes, then it is a chunk start
				if(chunkBufsize == 8)
				{
					//identify chunk
					dwFourCC = 0;
					dwSize= 0;

					dwFourCC = (unsigned int)(*chunkPtr);
					chunkPtr++;
					dwSize = (unsigned int)(*chunkPtr);
					
					//printf("dwFourCC = %d, dwSize = %d\n", dwFourCC, dwSize);
					
					//Add video frames
					if(dwFourCC == 0x62643030)
					{
						numVideoFrames++;
						//printf("\n No of video frames %d",numVideoFrames);
						//For writing to second next file when inserting I-frame .This is used for H264 encoding, as in H264 
						//encoding mode, we are waiting till next I-frame before closing file. So chunkHeader contains 
						//last i-frame header data.
						memset(chunkHeader, 0, 8);
						memcpy(chunkHeader, chunkBuf, 8);
						//printf("\n Frame size while storing header %d", dwSize);
					}
					
					chunkRemainBytes = dwSize;
					if (dwSize != 0)
					{
						if(dwSize%2 != 0)
						{
							chunkRemainBytes += 1;
						}
					}
				}
				else
				{
					//Remaining data of a chunk sometimes comes as a single byte
					//Exclude this case as this will be part of the previous chunk itself.
					if(chunkBufsize >1)
					{
						if(numIndex == indexCount)
						{
							numIndex += 256;
							aviIndex = (AVRM_AVI_Index_t*)realloc(aviIndex, (numIndex * sizeof(AVRM_AVI_Index_t)) );
						}	
						aviIndex[indexCount].fourCC = dwFourCC;
						
						if(dwFourCC == 0x62643030)
						{
							aviIndex[indexCount].flags = 0x12;//AVIIF_KEYFRAME;
							//Check if I-frame
						}
						else
						{
							aviIndex[indexCount].flags = 0;   //TBD
						}

						//aviIndex[indexCount].flags = AVIIF_KEYFRAME;
						aviIndex[indexCount].chunkOffset = (totalFileSize-8); //substract chunk header fourcc and size
						aviIndex[indexCount].chunkLength = dwSize;
						indexCount++;
					}
					chunkRemainBytes -= chunkBufsize;
				}
				totalFileSize += chunkBufsize;
				totalChunkSize += chunkBufsize;	
			}

			current = (GST_BUFFER_TIMESTAMP((GstBuffer*)curBuf)) / NANOTOMILLISECS;

			//Exit after record duration ends
			//printf("\n Total recording duration %d", (current - recording_start_time)/1000);
			if( ((current - recording_start_time)/1000) >= total_recording_duration)
			{
				//printf("\n Recording time ended");
				/**Recording is not stopped even if recording duration ends. But we set a flap to indicate 
					recording duration has ended. Current recording ends only after split file duration/size*/
				recording_duration_end = 1;
				//printf("\n Recording duration end\n");
				/*
				if(0 == chunkRemainBytes)
				{
					printf("\nRecording time ended.  Now ending file");
					fileEnd = 1;
					exit_after_recording_duration = 1;
				}
				else
				{
					printf("\nBut not ending file as remaining bytes > 0");

				}*/
			}
			//Copying every i-frame if mpeg4 encoding;
			if (gAVRM_handle->encType == AVRM_VID_ENC_MPEG4)
			{
				if( (dwFourCC == 0x62643030) && (chunkBufsize > 1) )
				{
					dwFourCC_video = dwFourCC;

					iframe = AVRM_Is_Iframe((char*)GST_BUFFER_DATA((GstBuffer*)curBuf), gAVRM_handle->encType);
					if (iframe)
					{
						iframe_size = GST_BUFFER_SIZE((GstBuffer*)curBuf);
						//If current frame is i-frame ,free previous i-frame memory as we need only last i-frame for next split file
						if (iframe_data != NULL)
						{
							free(iframe_data);
							iframe_data = NULL;
						}
						//Check if size is odd. If odd allocate memory for padding bytes also
						if (iframe_size%2 != 0)
						{
							iframe_size += 1;
							iframe_data = (unsigned char*)malloc(iframe_size*sizeof(unsigned char));
							if (iframe_data != NULL){
								memset(iframe_data, 0, (iframe_size * sizeof(unsigned char)));
							}
							//printf("\n Copying I frame of size %d", iframe_size);
							//Copy iframe_size -1, because padding byte will come in next iteration only.
							//This padding byte is not copied to iframe_data as it does not matter. 
							memcpy(iframe_data, (char*)GST_BUFFER_DATA((GstBuffer*)curBuf), iframe_size-1);
						}
						else
						{
							iframe_data = (unsigned char*)malloc(iframe_size*sizeof(unsigned char));
							if (iframe_data != NULL){
								memset(iframe_data, 0, (iframe_size * sizeof(unsigned char)));
							}
							//printf("\n Copying I frame of size %d", iframe_size);
							memcpy(iframe_data, (char*)GST_BUFFER_DATA((GstBuffer*)curBuf), iframe_size);
						}
					//update  size in header as the size will be updated for every frame in header parsing part
					//We need size of I-frame in header data 
					//memset(chunkHeader, 0, 8);
					//memcpy(chunkHeader+4, &iframe_size, 4);
					}
				}
			}
		
			if (file_duration != 0)
			{
				if (current != start)
				{
					//Commented below check because if we dont get I-frame right after the file duration ends, those data audio/video 
					//will not get written to file . So write audio/video date until  we get an I-frame

				//	if( ((current - start)/1000) <= file_duration)
				//	{
						//printf("\nWriting to file %d\n",(current - start)/1000);
#ifdef FILE_WRITE_BLOCK
						AVRM_filewrite((char*)GST_BUFFER_DATA((GstBuffer*)curBuf), GST_BUFFER_SIZE((GstBuffer*)curBuf), 0, fp);
#else
						fwrite((char*)GST_BUFFER_DATA((GstBuffer*)curBuf), 1, GST_BUFFER_SIZE((GstBuffer*)curBuf), fp);
						//printf("\n Writing to file");
						fflush(fp);
#endif

				//	}
						//printf("\nFile split duration %d\n",(current - start)/1000);
#ifdef VIDEOFRAMES_BASED_SPLIT
					if( numVideoFrames >= file_duration*framerate)
#else
					if( ((current - start)/1000) >= file_duration)
#endif
					{
						/*if (gexit_after_file_duration == 1)
						{	
							fileEnd = 1;
						}*/
						if (gAVRM_handle->encType == AVRM_VID_ENC_H264)
						{
							//Check if video frames and buffer is not a padding byte data
							if( (dwFourCC == 0x62643030) && (chunkBufsize > 1) )
							{
								//printf("\n Video : Current Timestamp  %"G_GUINT64_FORMAT "\n", current);
								//printf("\nFile duration reached. Writing video frames till next I-frame");
								iframe = AVRM_Is_Iframe((char*)GST_BUFFER_DATA((GstBuffer*)curBuf), gAVRM_handle->encType);
								if (iframe)
								{
									//iframe_count++;
									//printf("\n Iframe %d", iframe_count);
									iframe_size = GST_BUFFER_SIZE((GstBuffer*)curBuf);
									//Check if size is odd. If odd allocate memory for padding bytes also
									if (iframe_size%2 != 0)
									{
										iframe_size += 1;
										iframe_data = (unsigned char*)malloc(iframe_size*sizeof(unsigned char));
										if (iframe_data != NULL){
											memset(iframe_data, 0, (iframe_size * sizeof(unsigned char)));
										}
										//printf("\n Copying I frame of size %d", iframe_size);
										//Copy iframe_size -1, because padding byte will come in next iteration only.
										//This padding byte is not copied to iframe_data as it does not matter. 
										memcpy(iframe_data, (char*)GST_BUFFER_DATA((GstBuffer*)curBuf), iframe_size-1);
									}
									else
									{
										iframe_data = (unsigned char*)malloc(iframe_size*sizeof(unsigned char));
										if (iframe_data != NULL){
											memset(iframe_data, 0, (iframe_size * sizeof(unsigned char)));
										}
										//printf("\n Copying I frame of size %d", iframe_size);
										memcpy(iframe_data, (char*)GST_BUFFER_DATA((GstBuffer*)curBuf), iframe_size);
									}
								}
							}
							if(0 == chunkRemainBytes)
							{
								//End file only after getting I-frame and if remaining bytes = 0
								if (iframe)
								{
									iframe = 0;
									fileEnd = 1;
									identify_start_position = 1;
									//printf("\n File duration end");
								}
							}
						}
						else //MPEG4 Not waiting for next i_frame. Last I-frame is copied to next split file
						{
							if(0 == chunkRemainBytes)
							{
								fileEnd = 1;
								identify_start_position = 1;
								//printf("\n MPEG4 File duration end");
							}

						}
						/**For aligning last file duration to previous split files*/
						if (recording_duration_end)
						{
							recording_duration_end = 0;
							if(0 == chunkRemainBytes)
							{
								fileEnd = 1;
								exit_after_recording_duration = 1;
							}
						}
					}
				}
				else //For writing first buffer
				{
					//printf("\nFirst Write to file ");
#ifdef FILE_WRITE_BLOCK
					AVRM_filewrite((char*)GST_BUFFER_DATA((GstBuffer*)curBuf), GST_BUFFER_SIZE((GstBuffer*)curBuf), 0, fp);
#else
					fwrite((char*)GST_BUFFER_DATA((GstBuffer*)curBuf), 1, GST_BUFFER_SIZE((GstBuffer*)curBuf), fp);
					//printf("\n First write to file");
					fflush(fp);
#endif
				}	
			}
			else  //For file size splitting
			{
				//printf("\nFirst Write to file ");
#ifdef FILE_WRITE_BLOCK
				AVRM_filewrite((char*)GST_BUFFER_DATA((GstBuffer*)curBuf), GST_BUFFER_SIZE((GstBuffer*)curBuf), 0, fp);
#else
				fwrite((char*)GST_BUFFER_DATA((GstBuffer*)curBuf), 1, GST_BUFFER_SIZE((GstBuffer*)curBuf), fp);
				//printf("\n First write to file");
				fflush(fp);
#endif
				fret = stat(gAVRM_handle->cFile, &buf);
				//printf("Checking status of file %s Size %d KB\n",gAVRM_handle->cFile,(buf.st_size/1024));
				if(ret == 0 )
				{
					if((buf.st_size/1024) >= (gAVRM_handle->nFsize_in_mb*1024))
					{
						//Check if video frames and buffer is not a padding byte data
						if (gAVRM_handle->encType == AVRM_VID_ENC_H264)
						{
							if( (dwFourCC == 0x62643030) && (chunkBufsize > 1) )
							{
								//printf("\nFile size limit reached. Writing video frame till next I-frame");
								iframe = AVRM_Is_Iframe((char*)GST_BUFFER_DATA((GstBuffer*)curBuf), gAVRM_handle->encType);
								if (iframe)
								{
									iframe_size = GST_BUFFER_SIZE((GstBuffer*)curBuf);
									//Check if size is odd. If odd allocate memory for padding bytes also
									if (iframe_size%2 != 0)
									{
										iframe_size += 1;
										iframe_data = (unsigned char*)malloc(iframe_size*sizeof(unsigned char));
										if (iframe_data != NULL){
											memset(iframe_data, 0, (iframe_size * sizeof(unsigned char)));
										}
										//printf("\n Copying I frame of size %d", iframe_size);
										//Copy iframe_size -1, because padding byte will come in next iteration only.
										//This padding byte is not copied to iframe_data as it does not matter. 
										memcpy(iframe_data, (char*)GST_BUFFER_DATA((GstBuffer*)curBuf), iframe_size-1);
									}
									else
									{
										iframe_data = (unsigned char*)malloc(iframe_size*sizeof(unsigned char));
										if (iframe_data != NULL){
											memset(iframe_data, 0, (iframe_size * sizeof(unsigned char)));
										}
										//printf("\n Copying I frame of size %d", iframe_size);
										memcpy(iframe_data, (char*)GST_BUFFER_DATA((GstBuffer*)curBuf), iframe_size);
									}
								}
							}
							if(0 == chunkRemainBytes)
							{
								//End file only after getting I-frame and if remaining bytes = 0
								if (iframe)
								{
									iframe = 0;
									fileEnd = 1;
									identify_start_position = 1;
									//printf("\n File size reached");
								}
							}
						}
						else
						{
							if(0 == chunkRemainBytes)
							{
								fileEnd = 1;
								identify_start_position = 1;
								//printf("\n MPEG4 File  end");
							}
						}
						/**For aligning file size to previous split files*/
						if (recording_duration_end)
						{
							
							recording_duration_end = 0;
							if(0 == chunkRemainBytes)
							{
								printf("fileEnd = 1\n");
								fileEnd = 1;
								exit_after_recording_duration = 1;
							}
						}
					}
					/*	if(0 == chunkRemainBytes)
						{
						iframe = AVRM_Is_Iframe((char*)GST_BUFFER_DATA((GstBuffer*)curBuf), gAVRM_handle->encType);
						if (iframe)
						{
						iframe = 0;
						iframe_size = GST_BUFFER_SIZE((GstBuffer*)curBuf);
						iframe_data = (unsigned char*)malloc(iframe_size*sizeof(unsigned char));
						printf("\n Copying I-frame");
						memcpy(iframe_data, (char*)GST_BUFFER_DATA((GstBuffer*)curBuf), iframe_size);
						fileEnd = 1;
						identify_start_position = 1;
						}
						}*/
				}
			}
			if (eos && (chunkRemainBytes == 0))
			{
					fileEnd = 1;
			}

			//usleep(100000);


			if(fileEnd)
			{
				int i = 0;
				unsigned int buffer = 0;
				unsigned int moviPos = 0;
				unsigned int moviLen = 0;
				unsigned int curFilePos = 0;
				unsigned int bytesWritten = 0;
				unsigned int avihFlags = 0;

				printf("\nentered fileEnd check. \nAudio packets count in pull thread is %u and in file write thread is %u.\n",audio_packets_count_in_pull_thread,audio_data_count);

				avihFlags = AVRM_RIFF_AVIH_HASINDEX | AVRM_RIFF_AVIH_ISINTERLEAVED;

				fseek(fp, 0, SEEK_END);
				//Write general index
				buffer = 0;
				unsigned int indexLen = indexCount * sizeof(AVRM_AVI_Index_t);
  				memcpy (&buffer, "idx1", 4);
#ifdef FILE_WRITE_BLOCK
				bytesWritten = AVRM_filewrite(&buffer, 4, 0, fp);
#else
				bytesWritten = fwrite(&buffer, 1, 4, fp);
#endif
				memcpy(&buffer, &indexLen, 4);
#ifdef FILE_WRITE_BLOCK
				bytesWritten += AVRM_filewrite(&buffer, 4, 0, fp);
#else
				bytesWritten += fwrite(&buffer, 1, 4, fp);
				fflush(fp);
#endif
 
				//Update Index entries data
				for (i = 0; i < indexCount; i++)
				{
#ifdef FILE_WRITE_BLOCK
					if(i == (indexCount-1))
						bytesWritten += AVRM_filewrite(&(aviIndex[i]), sizeof(AVRM_AVI_Index_t), 1, fp);
					else
						bytesWritten += AVRM_filewrite(&(aviIndex[i]), sizeof(AVRM_AVI_Index_t), 0, fp);
#else
					bytesWritten += fwrite(&(aviIndex[i]), 1, sizeof(AVRM_AVI_Index_t), fp);
#endif
				}	
#ifndef FILE_WRITE_BLOCK			
				fflush(fp);
#endif
				
				
				//Update existing header data

				totalFileSize += bytesWritten;

				//RIFF header
				//exclude RIFF dwFourCC and size fields
				totalFileSize -= 8;

				fseek(fp, 4, SEEK_SET);
				memcpy(&buffer, &totalFileSize, 4);
#ifdef FILE_WRITE_BLOCK
				//force write as from now on all writes are seek based.
				AVRM_filewrite(&buffer, 4, 1, fp);
#else
				fwrite(&buffer, 1, 4, fp);
				fflush(fp);
#endif

				//printf("Number of video frames at file END: %d I-Frames %d\n\n\n", numVideoFrames, iframe_count);

				//update flags in avih
				fseek(fp, 44, SEEK_SET);
				memcpy(&buffer, &avihFlags, 4);
				fwrite(&buffer, 1, 4, fp);
				fflush(fp);

				//update number of frames in AVI header
				fseek(fp, 48, SEEK_SET);
				memcpy(&buffer, &numVideoFrames, 4);
				fwrite(&buffer, 1, 4, fp);
				fflush(fp);
					
				//update length field in strl header
				fseek(fp, 140, SEEK_SET);
				memcpy(&buffer, &numVideoFrames, 4);
				fwrite(&buffer, 1, 4, fp);
				fflush(fp);
				
				//Update existing movi header
				moviPos = headerDatasize;
				//skip back to size position
				moviPos -= 8;	
				moviLen = totalChunkSize + 4; //add length if 'movi' fourcc
				//printf("movi size position : %d\n", moviPos);			
				fseek(fp, moviPos, SEEK_SET);
				memcpy(&buffer, &moviLen, 4);
				fwrite(&buffer, 1, 4, fp);
				fflush(fp);


				fclose(fp);
				if (eos){
					//printf("\n File write thread : File closing and breaking  loop");
					file_closed = 1;
					break;
				}
				if (exit_after_recording_duration)
				{
					printf("if (exit_after_recording_duration) \n");				
					file_closed = 1;
					if(!gAVRM_handle->gbEosInitiated)
					{
						g_print ("Sending eos from file write thread\n");
						gst_element_send_event(gAVRM_handle->pipeline,gst_event_new_eos());
						usleep(10);
					}
					//AVRM_Stop();
					exit_after_recording_duration = 0;
					break;

				}
				fileEnd = 0;
			}

			gst_buffer_unref((GstBuffer*)curBuf);
			//printf("\n Unreffing");
		}
		else
		{
			printf("Ring buffer no data $$$$$$$$$$$$$$$$$$$$$%\n");
			usleep(30000);
		}
	}
	
	if(headerData != NULL)
		free(headerData);
	
	if(aviIndex != NULL)
	{
		free(aviIndex);
		aviIndex = NULL;
	}
	printf("\n File write thread exiting");
	pthread_exit(0);

} 
static GstFlowReturn onNewPrerollFromSink(GstElement *elt, int *data)
{
    GstBuffer *buffer = NULL;

    /* Retrieve the buffer */
    g_signal_emit_by_name (elt, "pull-preroll", &buffer);
    if (buffer)
    {
            /* The only thing we do in this example is print a * to indicate a received buffer */

            //printf("\nGOT preroll file split appsink");


            gst_element_set_state (gAVRM_handle->pipeline, GST_STATE_PLAYING);
	    //gst_element_set_state (elt, GST_STATE_PLAYING);
	    gst_element_set_state (gAVRM_handle->appsink, GST_STATE_PLAYING);
	    pthread_create (&thread_id, &detach_attr, (void*)appsink_pull_thread, NULL);
            gst_buffer_unref (buffer);
    }
}



static void RingBufMemFreeCallbk(void *ptr)
{
    gst_buffer_unref ((GstBuffer*)ptr);
}

static void* RingBufMemCopyCallbk(void *ptr)
{
    GstBuffer *copyBuf = NULL;
    copyBuf = gst_buffer_copy((GstBuffer*)ptr);

    return (void*)copyBuf;
}


#endif



eAVRM_RETURN AVRM_Init(eAVRM_MODE avrm_mode)
{
	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
	GstCaps *acaps;
	struct sched_param param;

	

	gst_init(NULL,NULL);
        /* To get error messages from gstreamer */
        gst_debug_set_default_threshold(1);
#if DEBUG 
	g_print("Inside AVRM_init, mode = %d\n",avrm_mode);
#endif
	gAVRM_handle =  (tAVRM_handle) malloc(sizeof(tAVRM_HandleStr));
	if(gAVRM_handle == NULL)
	{
		g_print("Could not create avrm-handle element\r\n");
		nRetVal = AVRM_FAILURE;
		goto labelExit;
	}

	gAVRM_handle->avrm_mode     = avrm_mode;
	gAVRM_handle->nWidth   = 0;
	gAVRM_handle->nHeight  = 0;
	gAVRM_handle->pipeline      = NULL;
	gAVRM_handle->omx_videosrc  = NULL;
	gAVRM_handle->omx_videoEnc  = NULL;
	gAVRM_handle->alsaAudSrc    = NULL;
	gAVRM_handle->AVIMux        = NULL;
	gAVRM_handle->FileSink      = NULL;
	gAVRM_handle->clockoverlay  = NULL;
	gAVRM_handle->audioQue = NULL;
	gAVRM_handle->audio_capsfilter = NULL;
	gAVRM_handle->nDuration = 30;
	gAVRM_handle->nFsize_in_mb = 0;
	gAVRM_handle->bResMisMatch = FALSE;
	gAVRM_handle->bFailedToGoPlayState = FALSE;
        gAVRM_handle->gbEosInitiated = FALSE;

	int pthread_ret;

	pthread_ret = pthread_attr_init(&detach_attr);
	if(pthread_ret != 0) {
		printf("\nError initalizing attributes");
		nRetVal = AVRM_FAILURE;
		goto labelExit;
	}
	pthread_attr_setdetachstate(&detach_attr,PTHREAD_CREATE_DETACHED);

	param.sched_priority=22;
        pthread_attr_setschedparam(&detach_attr,&param);

	memset(&dataBlock, 0, sizeof(AVRM_filedatablock_t));

	gAVRM_handle->pipeline = gst_pipeline_new ("avrm-pipeline");
	if(gAVRM_handle->pipeline == NULL)
	{
		g_print("Could not create 'avrm-pipleine' element\r\n");
		nRetVal = AVRM_FAILURE;
		goto labelExit;
	}

	/*************************************************************/


	if((avrm_mode == AVRM_AUDIO_VIDEO) || (avrm_mode == AVRM_VIDEO_ONLY) || (avrm_mode == AVRM_AUDIO_ONLY))
	{
		printf("\n if((avrm_mode == AVRM_AUDIO_VIDEO) || (avrm_mode == AVRM_VIDEO_ONLY) || (avrm_mode == AVRM_AUDIO_ONLY)) \n");
		if(avrm_mode == AVRM_AUDIO_ONLY)
		{
			/* PP comment : In only audio mode also need to feed video as 
			   blank frame.
			 */
			gAVRM_handle->omx_videosrc = gst_element_factory_make("videotestsrc", "omx_videosrc");
			if (gAVRM_handle->omx_videosrc == NULL)
			{ 
				g_print("Could not create 'videotestsrc' element\r\n");
				nRetVal = AVRM_FAILURE;
				goto labelExit;
			}
			else
			{
				g_object_set(G_OBJECT(gAVRM_handle->omx_videosrc), "pattern", 0, NULL);
				g_object_set(G_OBJECT(gAVRM_handle->omx_videosrc), "is-live", TRUE,NULL);
				//g_object_set(G_OBJECT(gAVRM_handle->omx_videosrc), "do-timestamp",TRUE,NULL);
			}

		}
		else
		{
			gAVRM_handle->omx_videosrc = gst_element_factory_make("omx_videosrc", "omx_videosrc");
			if (gAVRM_handle->omx_videosrc == NULL)
			{ 
				g_print("Could not create 'omx_videosrc' element\r\n");
				nRetVal = AVRM_FAILURE;
				goto labelExit;
			}
			else
			{
				g_object_set(G_OBJECT(gAVRM_handle->omx_videosrc), "do-timestamp",TRUE,NULL);
				//g_object_set(G_OBJECT(gAVRM_handle->omx_videosrc), "block-size",-1,NULL);
				/** PP comment: Resolution mismatch callback registration */
				g_signal_connect(G_OBJECT(gAVRM_handle->omx_videosrc),"vr_mismatch",(GCallback)(Video_Res_mismatch),NULL);
			}
		}
#ifdef OMX_BUFFER
		buffer_alloc = gst_element_factory_make("omxbufferalloc", "omx_bufferalloc");
		g_object_set(G_OBJECT(buffer_alloc), "numBuffers", (guint)10, NULL);
#endif

		gAVRM_handle->clockoverlay = gst_element_factory_make("clockoverlay", "clockoverlay");
		if (gAVRM_handle->clockoverlay == NULL)
		{ 
			g_print("Could not create 'clockoverlay' element\r\n");
			nRetVal = AVRM_FAILURE;
			goto labelExit;
		}
		else
		{
			g_object_set(G_OBJECT(gAVRM_handle->clockoverlay), "time-format","%d-%b-%Y / %H:%M:%S",NULL);
			g_object_set(G_OBJECT(gAVRM_handle->clockoverlay), "halignment",2,"valignment",1,NULL);  
		}

		gAVRM_handle->videoqueue = gst_element_factory_make("queue", "vencoutqueue");
		if (gAVRM_handle->videoqueue == NULL)
		{ 
			g_print("Could not create 'video queue' element\r\n");
			nRetVal = AVRM_FAILURE;
			goto labelExit;
		}
#ifdef FILESPLIT_APPSINK
		gAVRM_handle->appsink = gst_element_factory_make ("appsink", "appsink");
		if (gAVRM_handle->appsink == NULL)
		{ 
			g_print("Could not create 'appsink' element\r\n");
			nRetVal = AVRM_FAILURE;
			goto labelExit;
		}
		else
		{
			g_object_set(G_OBJECT(gAVRM_handle->appsink), "emit-signals", TRUE, "sync", FALSE, "enable-last-buffer", FALSE, NULL);
			//g_object_set(G_OBJECT(gAVRM_handle->appsink), "emit-signals", TRUE, NULL);
			//g_object_set(G_OBJECT(gAVRM_handle->appsink), "blocksize", 15360, NULL);  //15k data
			g_object_set(G_OBJECT(gAVRM_handle->appsink), "max-buffers", 1000, NULL);  //15k data
			//g_object_set(G_OBJECT(gAVRM_handle->appsink), "drop", TRUE, NULL);  //15k data
			//g_object_set(G_OBJECT(gAVRM_handle->appsink), "async", FALSE, NULL);  //15k data
			//g_signal_connect(gAVRM_handle->appsink, "new-buffer", G_CALLBACK(onNewBufferFromAppSink), NULL);
			g_signal_connect(gAVRM_handle->appsink, "new-preroll", G_CALLBACK(onNewPrerollFromSink), NULL);
		}
		nRetVal = RingBufCreate(&(ringBufHandle), NUM_BUFFERS);
		if(nRetVal != AVRM_SUCCESS)
		{
			printf("RingBufCreate Failed\n");
			goto labelExit;
		}
		//Register mem free callback for ring buffer
		RingBufRegMemFreeCb(ringBufHandle, RingBufMemFreeCallbk );
		RingBufRegMemCopyCb(ringBufHandle, RingBufMemCopyCallbk );

		sem_init(&ringBufReady, 0, 0);

		if(0 != pthread_create (&(gAVRM_handle->filewrite_thread), &detach_attr, (void*)file_write_thread, NULL))
		{

			g_print("Falied to create file_write_thread thread\n");
			nRetVal = AVRM_FAILURE;
			goto labelExit;

		}

#endif
		/* Default video encoder is H264. We will change this later if user opts for MPEG-4 */
		gAVRM_handle->omx_videoEnc = gst_element_factory_make("omx_h264enc", "omx_h264enc");
		if (gAVRM_handle->omx_videoEnc == NULL)
		{ 
			g_print("Could not create 'omx_videoEnc' element\r\n");
			nRetVal = AVRM_FAILURE;
			goto labelExit;
		}

		if (avrm_mode == AVRM_AUDIO_VIDEO || avrm_mode == AVRM_AUDIO_ONLY)
		{
			printf("\n Inside if (avrm_mode == AVRM_AUDIO_VIDEO || avrm_mode == AVRM_AUDIO_ONLY) \n");
			gAVRM_handle->alsaAudSrc = gst_element_factory_make("alsasrc", "alsasrc");
			//gAVRM_handle->alsaAudSrc = gst_element_factory_make("audiotestsrc", "audiotestsrc");
			if (gAVRM_handle->alsaAudSrc == NULL)
			{ 
				g_print("Could not create 'audio src' element\r\n");
				nRetVal = AVRM_FAILURE;
				goto labelExit;
			}
			else
			{
#if 0
				guint64 actLatency = 0;
				guint64 actbuftime = 0;
				guint64 latency = 0;
				guint64 buftime = 0;
#endif
				g_object_set(G_OBJECT(gAVRM_handle->alsaAudSrc), "blocksize",(gint64)4096, NULL);
				//g_object_set(G_OBJECT(gAVRM_handle->alsaAudSrc), "blocksize",(gint64)4410, NULL);
				g_object_set(G_OBJECT(gAVRM_handle->alsaAudSrc), "do-timestamp",TRUE,NULL);
				g_object_set(G_OBJECT(gAVRM_handle->alsaAudSrc), "buffer-time",(guint64)8000000,NULL);
				g_object_set(G_OBJECT(gAVRM_handle->alsaAudSrc), "latency-time",(guint64)40000,NULL);
				g_object_set(G_OBJECT(gAVRM_handle->alsaAudSrc), "slave-method",0,NULL);

				//g_object_set(G_OBJECT(gAVRM_handle->alsaAudSrc), "buffer-time",(guint64)10000000,NULL);
				//g_object_set(G_OBJECT(gAVRM_handle->alsaAudSrc), "latency-time",(guint64)5000000,NULL);
#if 0
				g_object_get(G_OBJECT(gAVRM_handle->alsaAudSrc), "latency-time",&latency,NULL);
				printf("\n Getting alsasrc latency time: %d\n", latency);
				g_object_get(G_OBJECT(gAVRM_handle->alsaAudSrc), "buffer-time",&buftime,NULL);
				printf("\n Getting alsasrc buf time: %d\n", buftime);
				g_object_get(G_OBJECT(gAVRM_handle->alsaAudSrc), "actual-latency-time",&actLatency,NULL);
				printf("\n Getting alsasrc actual latency time: %d\n", actLatency);
				g_object_get(G_OBJECT(gAVRM_handle->alsaAudSrc), "actual-buffer-time",&actbuftime,NULL);
				printf("\n Getting alsasrc actual buf time: %d\n", actbuftime);
				//g_object_set(G_OBJECT(gAVRM_handle->alsaAudSrc), "num-buffers",25000,NULL);
#endif
			}
			gAVRM_handle->audioQue = gst_element_factory_make("queue", "aqueue");
			if (gAVRM_handle->audioQue == NULL)
			{ 
				g_print("Could not create 'audio queue' element\r\n");
				nRetVal = AVRM_FAILURE;
				goto labelExit;
			}
			else
			{
				printf("\n Setting audio queue min threshold buffers\n");
				//g_object_set(G_OBJECT(gAVRM_handle->audioQue), "min-threshold-buffers",(guint)22,NULL);
				//g_object_set(G_OBJECT(gAVRM_handle->audioQue), "min-threshold-bytes",(guint)90112,NULL);
				//g_object_set(G_OBJECT(gAVRM_handle->audioQue), "min-threshold-time",(guint64)500000000,NULL);				
				//g_object_set(G_OBJECT(gAVRM_handle->audioQue), "min-threshold-buffers",(guint)50,NULL);
				//g_object_set(G_OBJECT(gAVRM_handle->audioQue), "min-threshold-bytes",(guint)204800,NULL);
			}
			gAVRM_handle->audio_capsfilter = gst_element_factory_make("capsfilter","audiocaps");
			if (gAVRM_handle->audio_capsfilter == NULL)
			{ 
				g_print("Could not create 'audio caps filter' element\r\n");
				nRetVal = AVRM_FAILURE;
				goto labelExit;
			}
			else
			{
				acaps = gst_caps_new_simple ( "audio/x-raw-int",
						"endianness", G_TYPE_INT, 1234, 
						"signed", G_TYPE_BOOLEAN, TRUE,
						"width", G_TYPE_INT, 16,
						"depth", G_TYPE_INT, 16, 
						//"rate", G_TYPE_INT, 44100,
						"rate", G_TYPE_INT, 48000,  
						"channels", G_TYPE_INT, 2, 
						NULL);
				g_object_set (G_OBJECT(gAVRM_handle->audio_capsfilter), "caps", acaps, NULL);
				gst_caps_unref (acaps);
			}

			gAVRM_handle->audioRate = gst_element_factory_make("audiorate","audiorate");
			if (gAVRM_handle->audioRate == NULL)
			{ 
				g_print("Could not create 'audioRate' element\r\n");
				nRetVal = AVRM_FAILURE;
				goto labelExit;
			}
			else
			{
				g_object_set (G_OBJECT(gAVRM_handle->audioRate), "silent", FALSE, NULL);
				g_object_set (G_OBJECT(gAVRM_handle->audioRate), "tolerance", (50*1000*1000), NULL);
			}
			gAVRM_handle->audioPerf = gst_element_factory_make("gstperf","gstaudperf");
			if (gAVRM_handle->audioPerf == NULL)
			{ 
				g_print("Could not create 'audioperf' element\r\n");
				nRetVal = AVRM_FAILURE;
				goto labelExit;
			}
			else
			{
				//g_object_set (G_OBJECT(gAVRM_handle->audioRate), "silent", FALSE, NULL);
				//g_object_set (G_OBJECT(gAVRM_handle->audioRate), "tolerance", (200*1000*1000), NULL);
			}
			gAVRM_handle->videoPerf = gst_element_factory_make("gstperf","gstvideoperf");
			if (gAVRM_handle->videoPerf == NULL)
			{ 
				g_print("Could not create 'videoperf' element\r\n");
				nRetVal = AVRM_FAILURE;
				goto labelExit;
			}
			else
			{
				//g_object_set (G_OBJECT(gAVRM_handle->audioRate), "silent", FALSE, NULL);
				//g_object_set (G_OBJECT(gAVRM_handle->audioRate), "tolerance", (200*1000*1000), NULL);
			}

		}

		//gAVRM_handle->AVIMux = gst_element_factory_make("matroskamux","mux");
		gAVRM_handle->AVIMux = gst_element_factory_make("avimux","mux");
		if (gAVRM_handle->AVIMux == NULL)
		{
			g_print("Couldn't create matroska mux element");
			nRetVal = AVRM_FAILURE;
			goto labelExit;
		}
#ifndef FILESPLIT_APPSINK
		gAVRM_handle->FileSink = gst_element_factory_make("filesink", "filesink");
		if (gAVRM_handle->FileSink == NULL)
		{ 
			g_print("Could not create 'filesink' element\r\n");
			nRetVal = AVRM_FAILURE;
			goto labelExit;
		}
		else
		{
			g_object_set(G_OBJECT(gAVRM_handle->FileSink), "location","rec_120415_101112.avi",NULL);
		}
#endif
	}
	else
	{
		g_print("Invalid mode for AVRM\n");
		nRetVal = AVRM_INVALID_PARAM;
		goto labelExit;

	}

labelExit:
	g_print("Exiting init func\n");
	if(nRetVal == AVRM_FAILURE){
		if(gAVRM_handle->pipeline)
		{
			gst_object_unref (GST_OBJECT(gAVRM_handle->pipeline));
		}
		if(gAVRM_handle->omx_videosrc)
		{
			gst_object_unref (GST_OBJECT(gAVRM_handle->omx_videosrc));
		}
		if(gAVRM_handle->omx_videoEnc)
		{
			gst_object_unref (GST_OBJECT(gAVRM_handle->omx_videoEnc));
		}
		if(gAVRM_handle->alsaAudSrc)
		{
			gst_object_unref (GST_OBJECT(gAVRM_handle->alsaAudSrc));
		}
		if(gAVRM_handle->AVIMux)
		{
			gst_object_unref (GST_OBJECT(gAVRM_handle->AVIMux));
		}
#ifndef FILESPLIT_APPSINK
		if(gAVRM_handle->FileSink)
		{
			gst_object_unref (GST_OBJECT(gAVRM_handle->FileSink));
		}
#endif
		if(gAVRM_handle->clockoverlay)
		{
			gst_object_unref (GST_OBJECT(gAVRM_handle->clockoverlay));
		}
		if(gAVRM_handle->audioQue)
		{
			gst_object_unref (GST_OBJECT(gAVRM_handle->audioQue));
		}
		if(gAVRM_handle->audio_capsfilter)
		{
			gst_object_unref (GST_OBJECT(gAVRM_handle->audio_capsfilter));
		}
#ifdef FILESPLIT_APPSINK
		if(gAVRM_handle->videoqueue)
		{
			gst_object_unref (GST_OBJECT(gAVRM_handle->videoqueue));
		}
		if(gAVRM_handle->appsink)
		{
			gst_object_unref (GST_OBJECT(gAVRM_handle->appsink));
		}
#endif
	}
	return nRetVal;

}

eAVRM_RETURN AVRM_SetVidCaptSource ( eAVRM_VIDSRC eVidSrc)
{

	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
	INT32 fd;
	INT32 ret = 0;

	if ((fd = open(FPGA_DVI_GPIO_VAL, O_WRONLY)) < 0)
	{
		g_print("Couldn't open sysfs value attribute for gpio line in write mode\n");
		nRetVal = AVRM_FAILURE;
		goto labelExit;
	}

	if (eVidSrc == AVRM_VIDSRC_FPGA)
	{
#if 0
		if (system("echo 0 > "FPGA_DVI_GPIO_VAL) < 0)
		{
			nRetVal = AVRM_FAILURE;
		}
#elif 1

		ret = write(fd,"0",1);
		usleep(100);
		if (ret == 0 || ret < 0)
		{
			g_print("Couldn't write to sysfs value attribute for gpio line\n");
			nRetVal = AVRM_FAILURE;
			goto labelExit;
		}
#endif

	}

	else if (eVidSrc == AVRM_VIDSRC_DVI)
	{
#if 0
		if (system("echo 1 > "FPGA_DVI_GPIO_VAL) < 0)
		{   
			nRetVal = AVRM_FAILURE;
		}
#elif 1
		ret = write(fd,"1",1);
		usleep(100);
		if (ret == 0 || ret < 0)
		{
			g_print("Couldn't write to sysfs value attribute for gpio line\n");
			nRetVal = AVRM_FAILURE;
			goto labelExit;
		}
#endif
	}
	else
	{
		nRetVal = AVRM_INVALID_PARAM;
		goto labelExit;
	}


labelExit:
	close(fd);
	return nRetVal;

}

eAVRM_RETURN AVRM_GetVidCaptSource ( eAVRM_VIDSRC *eVidSrc)
{
	INT8 gpio_val = 254;
	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
	INT32 fd;
	INT32 ret = 0;

	if ((fd = open(FPGA_DVI_GPIO_VAL, O_RDONLY)) < 0)
	{
		g_print("Couldn't open sysfs value attribute for gpio line in read mode\n");
		nRetVal = AVRM_FAILURE;
		goto labelExit;
	}

	ret = read(fd,&gpio_val,1);
	if (ret == 0 || ret < 0)
	{
		g_print("Couldn't read from sysfs value attribute for gpio line\n");
		nRetVal = AVRM_FAILURE;
		goto labelExit;
	}

	if (gpio_val == '0')
	{
		*eVidSrc = AVRM_VIDSRC_FPGA;
	}
	else if (gpio_val == '1')
	{   
		*eVidSrc = AVRM_VIDSRC_DVI;
	}
	else
	{
		*eVidSrc = AVRM_VIDSRC_ERR;
		nRetVal = AVRM_FAILURE;
		goto labelExit;
	}

labelExit:
	close(fd);  
	return nRetVal;
}

eAVRM_RETURN AVRM_SetVidCaptResolution ( eAVRM_RESOLUTION eResolution)
{
	gchar *mode = NULL;
	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
	if(gAVRM_handle->avrm_mode != AVRM_AUDIO_ONLY)
	{
		switch(eResolution)
		{
			case AVRM_RES_640_480:
				mode    = "480p";
				gAVRM_handle->nWidth         = 640;
				gAVRM_handle->nHeight        = 480;
				break;
			case AVRM_RES_800_600:
				mode    = "800_600p";
				gAVRM_handle->nWidth         = 800;
				gAVRM_handle->nHeight        = 600;
				break;
			case AVRM_RES_848_480:
				mode    = "848_480p";
				gAVRM_handle->nWidth         = 848;
				gAVRM_handle->nHeight        = 480;
				break;
			case AVRM_RES_1024_768:
				mode    = "1024_768p";
				gAVRM_handle->nWidth         = 1024;
				gAVRM_handle->nHeight        = 768;
				break;
			case AVRM_RES_1280_768:
				mode    = "1280_768p";
				gAVRM_handle->nWidth         = 1280;
				gAVRM_handle->nHeight        = 768;
				break;
			case AVRM_RES_1280_800:
				mode    = "1280_800p";
				gAVRM_handle->nWidth         = 1280;
				gAVRM_handle->nHeight        = 800;
				break;
			case AVRM_RES_1280_960:
				mode    = "1280_960p";
				gAVRM_handle->nWidth         = 1280;
				gAVRM_handle->nHeight        = 960;
				break;
			case AVRM_RES_1280_1024:
				mode    = "1280_1024p";
				gAVRM_handle->nWidth         = 1280;
				gAVRM_handle->nHeight        = 1024;
				break;
			case AVRM_RES_1360_768:
				mode    = "1360_768p";
				gAVRM_handle->nWidth         = 1360;
				gAVRM_handle->nHeight        = 768;
				break;
			case AVRM_RES_1400_1050:
				mode    = "1400_1050p";
				gAVRM_handle->nWidth         = 1400;
				gAVRM_handle->nHeight        = 1050;
				break;
			case AVRM_RES_1440_900:
				mode    = "1440_900p";
				gAVRM_handle->nWidth         = 1440;
				gAVRM_handle->nHeight        = 900;
				break;
			case AVRM_RES_1600_900:
				mode    = "1600_900p";
				gAVRM_handle->nWidth         = 1600;
				gAVRM_handle->nHeight        = 900;
				break;
			case AVRM_RES_1600_1200:
				mode    = "1600_1200p";
				gAVRM_handle->nWidth         = 1600;
				gAVRM_handle->nHeight        = 1200;
				break;
			case AVRM_RES_1680_1050:
				mode    = "1680_1050p";
				gAVRM_handle->nWidth         = 1680;
				gAVRM_handle->nHeight        = 1050;
				break;

			case AVRM_RES_1920_1080:
				mode    = "1080p";
				gAVRM_handle->nWidth         = 1920;
				gAVRM_handle->nHeight        = 1080;
				break;
			case AVRM_RES_1920_1200:
				mode    = "1200p";
				gAVRM_handle->nWidth         = 1920;
				gAVRM_handle->nHeight        = 1200;
				break;
			default:
				nRetVal = AVRM_INVALID_PARAM;
				break;

		}

		if (mode != NULL)
		{
			g_object_set(G_OBJECT(gAVRM_handle->omx_videosrc), "mode",mode,NULL);
		}
	}
	return nRetVal;
}

eAVRM_RETURN AVRM_GetVidCaptResolution ( INT32 *width, INT32 *height)
{
	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
	if(gAVRM_handle->avrm_mode != AVRM_AUDIO_ONLY)
	{
		*width = gAVRM_handle->nWidth;
		*height = gAVRM_handle->nHeight;
	}
	else
	{
		*width = 0;
		*height = 0;
	}

	return nRetVal;
}

eAVRM_RETURN AVRM_ClearVidCaptResolution ()
{
	if(gAVRM_handle->avrm_mode != AVRM_AUDIO_ONLY)
	{
		g_object_set(G_OBJECT(gAVRM_handle->omx_videosrc), "mode",DEFAULT_CAPT_MODE,NULL);
	}
	return AVRM_SUCCESS;
}

eAVRM_RETURN AVRM_SetVidCaptFPS(eAVRM_CAPT_FPS fps)
{
	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
	if(gAVRM_handle->avrm_mode != AVRM_AUDIO_ONLY)
	{
		switch(fps)
		{
			case AVRM_CAPT_FPS_4:
				g_object_set(G_OBJECT(gAVRM_handle->omx_videosrc), "framerate",4,NULL);
				framerate = 4;
				break;
			case AVRM_CAPT_FPS_6:
				g_object_set(G_OBJECT(gAVRM_handle->omx_videosrc), "framerate",6,NULL);
				framerate = 6;
				break;
			case AVRM_CAPT_FPS_10:
				g_object_set(G_OBJECT(gAVRM_handle->omx_videosrc), "framerate",10,NULL);
				framerate = 10;
				break;
			case AVRM_CAPT_FPS_12:
				g_object_set(G_OBJECT(gAVRM_handle->omx_videosrc), "framerate",12,NULL);
				framerate = 12;
				break;
			case AVRM_CAPT_FPS_15:
				g_object_set(G_OBJECT(gAVRM_handle->omx_videosrc), "framerate",15,NULL);
				framerate = 15;
				break;
			case AVRM_CAPT_FPS_30:
				g_object_set(G_OBJECT(gAVRM_handle->omx_videosrc), "framerate",30,NULL);
				framerate = 30;
				break;
			default:
				nRetVal = AVRM_INVALID_PARAM;
				break;
		}
	}
	return nRetVal;    
}

eAVRM_RETURN AVRM_GetVidCaptFPS(eAVRM_CAPT_FPS *fps)
{

	INT32 framerate = 0;
	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
	if(gAVRM_handle->avrm_mode != AVRM_AUDIO_ONLY)
	{
		g_object_get(G_OBJECT(gAVRM_handle->omx_videosrc), "framerate",&framerate,NULL);

		switch(framerate)
		{
			case 4:
				*fps = AVRM_CAPT_FPS_4;
				break;
			case 6:
				*fps = AVRM_CAPT_FPS_6;
				break;
			case 10:
				*fps = AVRM_CAPT_FPS_10;
				break;
			case 12:
				*fps = AVRM_CAPT_FPS_12;
				break;
			case 15:
				*fps = AVRM_CAPT_FPS_15;
				break;
			case 30:
				*fps = AVRM_CAPT_FPS_30;
				break;
			default:
				*fps = AVRM_CAPT_FPS_ERR;
				nRetVal = AVRM_FAILURE;
				break;
		}
	}
	return nRetVal;    
}

eAVRM_RETURN AVRM_SetVidEncConfig( eAVRM_VID_ENC_TYPE enc, eAVRM_VIDENC_PROF profile, INT32 bitrate)
{

	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
	gAVRM_handle->encType = AVRM_VID_ENC_MAX;
	//gAVRM_handle->omx_videoEnc = NULL;
	eAVRM_CAPT_FPS fps;
	INT32 framerate = 0;

	g_object_get(G_OBJECT(gAVRM_handle->omx_videosrc), "framerate",&framerate,NULL);
	if (enc == AVRM_VID_ENC_H264)
	{

		if ((profile == AVRM_H264_BL || profile == AVRM_H264_ML || profile == AVRM_H264_HL) && ( bitrate >= 500000))
		{   

			//gAVRM_handle->omx_videoEnc = gst_element_factory_make("omx_h264enc", "omx_h264enc");
			if (gAVRM_handle->omx_videoEnc == NULL)
			{
				g_print("failed to create h264 encoder element\n");
				nRetVal = AVRM_FAILURE;
				goto labelExit;
			}
			else
			{
				//printf("\n Frame rate got in h264 encoder to set I-frame periodicity %d", framerate);
			
				g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "bitrate",bitrate,NULL);
				g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "profile",profile,NULL);
				printf("omx_h264enc setting i-period and force-idr-period\n");
				g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "i-period",(framerate-1),NULL);
				g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "force-idr-period",(framerate-1),NULL);
				//g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "use-timestamps", FALSE,NULL);
				//g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "gen-timestamps", FALSE,NULL);
				//g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "framerate", framerate,NULL);
				//g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "input-buffers", 10, NULL);
				g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "output-buffers", 16,NULL);
				g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "bytestream", TRUE,NULL);
			}
		}
		else
		{

			g_print("Invalid profile %d for selected encoder %d\n",profile,enc);
			nRetVal = AVRM_INVALID_PARAM;
			goto labelExit;
		}

		gAVRM_handle->encType = AVRM_VID_ENC_H264;      

	}
	else if (enc == AVRM_VID_ENC_MPEG4)
	{

		if (profile != AVRM_MPEG4_SIMPLE)
		{
			nRetVal = AVRM_INVALID_PARAM;
			goto labelExit;
		}
		else
		{
//#define FFENC
#ifdef FFENC
			gAVRM_handle->omx_videoEnc = gst_element_factory_make("ffenc_mpeg4", "ffenc");
#else
			gAVRM_handle->omx_videoEnc = gst_element_factory_make("omx_mpeg4enc", "omx_mpeg4enc");
#endif
			if (gAVRM_handle->omx_videoEnc == NULL)
			{
				g_print("failed to create mpeg4 encoder element\n");
				nRetVal = AVRM_FAILURE;
				goto labelExit;
			}
			else
			{
#ifdef FFENC
				g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "max-key-interval",29,NULL);
#else
				g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "bitrate",bitrate,NULL);
				g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "profile",profile,NULL);
				//g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "framerate", framerate,NULL);
				//g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "input-buffers", 10, NULL);
				g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "output-buffers", 16,NULL);
				//g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "i-period",framerate,NULL);
#endif
			}
		}

		gAVRM_handle->encType = AVRM_VID_ENC_MPEG4;     

	}
	else
	{
		nRetVal = AVRM_INVALID_PARAM;
	}

labelExit:
	return nRetVal;
}
eAVRM_RETURN AVRM_SetAudioCh(UINT32 channel)
{
	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
	GstCaps *acaps;
	//gAVRM_handle->audio_capsfilter = gst_element_factory_make("capsfilter","audiocaps");
	if (gAVRM_handle->audio_capsfilter == NULL)
	{ 
		g_print("Could not create 'audio caps filter' element\r\n");
		nRetVal = AVRM_FAILURE;
	}
	else
	{
		acaps = gst_caps_new_simple ( "audio/x-raw-int",
				"width", G_TYPE_INT, 16,
				"depth", G_TYPE_INT, 16, 
				//"rate", G_TYPE_INT, 44100,
				"rate", G_TYPE_INT, 48000,  
				"channels", G_TYPE_INT, channel, 
				NULL);
		g_object_set (G_OBJECT(gAVRM_handle->audio_capsfilter), "caps", acaps, NULL);
		gst_caps_unref (acaps);
		nRetVal = AVRM_SUCCESS;
	}
	return nRetVal; 
}
eAVRM_RETURN AVRM_GetVidEncConfig(eAVRM_VID_ENC_TYPE *enc, eAVRM_VIDENC_PROF *profile, INT32 *bitrate)
{

	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
	/* Local Variables */   
	eAVRM_VIDENC_PROF lProfile; 
	INT32 lBitrate;

	*enc = gAVRM_handle->encType;
	*profile = AVRM_VIDENC_PROF_ERR;
	*bitrate = 0;

	if (*enc == AVRM_VID_ENC_H264 || *enc ==AVRM_VID_ENC_MPEG4)
	{

		g_object_get(G_OBJECT(gAVRM_handle->omx_videoEnc), "bitrate",&lBitrate,NULL);
		g_object_get(G_OBJECT(gAVRM_handle->omx_videoEnc), "profile",&lProfile,NULL);

		*bitrate = lBitrate;
		switch(lProfile)
		{
			case 1:
				if(*enc == AVRM_VID_ENC_H264)
					*profile = AVRM_H264_BL;
				else
					*profile = AVRM_MPEG4_SIMPLE;
				break;

			case 2:
				if(*enc == AVRM_VID_ENC_H264)
					*profile = AVRM_H264_ML;
				break;

			case 8:
				if(*enc == AVRM_VID_ENC_H264)
					*profile = AVRM_H264_HL;
				break;
			default:
				*profile = AVRM_VIDENC_PROF_ERR;
				break;

		}
	}
	else
	{
		nRetVal = AVRM_FAILURE;
	}

	return nRetVal;
}

eAVRM_RETURN AVRM_ClearVidEncConfig()
{

	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
	gAVRM_handle->encType = AVRM_VID_ENC_MAX;
	gAVRM_handle->omx_videoEnc = gst_element_factory_make("omx_h264enc", "omx_h264enc");
	if (gAVRM_handle->omx_videoEnc == NULL)
	{
		g_print("failed to create h264 encoder element\n");
		nRetVal = AVRM_FAILURE;
		goto labelExit;
	}
	else
	{
		g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "bitrate",500000,NULL);
		g_object_set(G_OBJECT(gAVRM_handle->omx_videoEnc), "profile",AVRM_H264_BL,NULL);
		gAVRM_handle->encType = AVRM_VID_ENC_H264;
	}

labelExit:
	return nRetVal;

}

eAVRM_RETURN AVRM_SelectFileLocation(char* loc)
{
	strcpy(gAVRM_handle->cFilePath, loc);
	printf("file path is %s\n",gAVRM_handle->cFilePath);
	return AVRM_SUCCESS;
}
eAVRM_RETURN AVRM_SetTotalCaptDuration(UINT32 duration)
{
	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
#ifdef FILESPLIT_APPSINK
	total_recording_duration = duration;
#endif
	return nRetVal;

}
eAVRM_RETURN AVRM_SetCaptFileSize(UINT32 size_in_mb)
{
	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
	if(size_in_mb < 0)
	{
		nRetVal = AVRM_FAILURE;
	}
	else
	{
		g_print("required file size = %lu\n",size_in_mb);
		gAVRM_handle->nFsize_in_mb = size_in_mb;
		gAVRM_handle->nDuration = 0; 
#ifdef FILESPLIT_APPSINK
		if (size_in_mb != 0)
			file_duration = 0;
#endif
	}
	return nRetVal; 
}
eAVRM_RETURN AVRM_SetCaptFileDuration(UINT32 seconds)
{

	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
	INT32 framerate = 0;
	INT32 num_buffers = 0;
	if(seconds < 0)
	{
		nRetVal = AVRM_FAILURE;
	}
	else
	{
		gAVRM_handle->nDuration = seconds;
#ifdef FILESPLIT_APPSINK
		if (gAVRM_handle->nFsize_in_mb == 0){
			file_duration = seconds;
		}
#endif
		gAVRM_handle->nFsize_in_mb = 0;
		if(gAVRM_handle->avrm_mode == AVRM_VIDEO_ONLY)
		{
			g_object_get(G_OBJECT(gAVRM_handle->omx_videosrc), "framerate",&framerate,NULL);
			if (framerate)
			{
				if ((num_buffers = seconds*framerate ) > 2147483647 )
				{
					num_buffers = 2147483647;
				}
				g_object_set(G_OBJECT(gAVRM_handle->omx_videosrc), "num-buffers",num_buffers,NULL);
			}
		}

	}

	return nRetVal;  
}
eAVRM_RETURN setFileName()
{
	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
	time_t rawtime;
	struct tm * timeinfo;
	char fname[30];

	time ( &rawtime );
	timeinfo = localtime ( &rawtime );
#ifndef FILESPLIT_APPSINK
	strftime (fname,30,"/rec_%d%m%y_%H%M%S_split1.avi",timeinfo);
	strcat(gAVRM_handle->cFilePath,fname);
	g_object_set(G_OBJECT(gAVRM_handle->FileSink), "location",gAVRM_handle->cFilePath, NULL);
#else
	memset(gAVRM_handle->fname,0,sizeof(gAVRM_handle->fname));
	strftime (gAVRM_handle->fname,30,"/rec_%d%m%y_%H%M%S.avi",timeinfo);
#endif
	return nRetVal; 
}

eAVRM_RETURN AVRM_GetCreatedFileName(INT8 *fname)
{    
	strcpy(fname, gAVRM_handle->cFile);
	return AVRM_SUCCESS; 
}
eAVRM_RETURN AVRM_Start()
{
	eAVRM_RETURN nRetVal = AVRM_SUCCESS;

	eAVRM_MODE avrm_mode = gAVRM_handle->avrm_mode;

	GstPadLinkReturn lres;

	eos = 0; //reset eos 

	if (avrm_mode == AVRM_AUDIO_VIDEO || avrm_mode == AVRM_VIDEO_ONLY || avrm_mode == AVRM_AUDIO_ONLY)
	{
#ifdef FILESPLIT_APPSINK
		gst_bin_add_many(GST_BIN(gAVRM_handle->pipeline), gAVRM_handle->AVIMux,
				gAVRM_handle->appsink,NULL);
		gst_bin_add_many(GST_BIN(gAVRM_handle->pipeline), gAVRM_handle->omx_videosrc,/*videorate, */
				 gAVRM_handle->clockoverlay ,gAVRM_handle->omx_videoEnc, /*gAVRM_handle->videoPerf,*/ gAVRM_handle->videoqueue, NULL);
#ifdef H264_DUMP
		if( TRUE != gst_element_link_many(gAVRM_handle->omx_videosrc, /*videorate,*/ gAVRM_handle->clockoverlay,
					gAVRM_handle->omx_videoEnc, gAVRM_handle->videoqueue, gAVRM_handle->appsink, NULL))
#else
		if( TRUE != gst_element_link_many(gAVRM_handle->omx_videosrc, /*videorate,*/ gAVRM_handle->clockoverlay ,
					gAVRM_handle->omx_videoEnc, /*gAVRM_handle->videoPerf,*/ gAVRM_handle->videoqueue, NULL))
#endif
		{
			g_print("Failed to link video plugins\n");
			nRetVal = AVRM_FAILURE;
			goto labelExit;
		}
#else
		gst_bin_add_many(GST_BIN(gAVRM_handle->pipeline), gAVRM_handle->AVIMux, 
				gAVRM_handle->FileSink,NULL);
		gst_bin_add_many(GST_BIN(gAVRM_handle->pipeline), gAVRM_handle->omx_videosrc, 
				gAVRM_handle->clockoverlay, gAVRM_handle->omx_videoEnc,gAVRM_handle->videoqueue,NULL);
		if( TRUE != gst_element_link_many(gAVRM_handle->omx_videosrc, gAVRM_handle->clockoverlay,
					gAVRM_handle->omx_videoEnc, gAVRM_handle->videoqueue, NULL))
		{
			g_print("Failed to link video plugins\n");
			nRetVal = AVRM_FAILURE;
			goto labelExit;
		}                
#endif
#ifndef H264_DUMP
                videoqueue_src_pad = gst_element_get_static_pad (gAVRM_handle->videoqueue, "src");

                avimux_video_sink_pad = gst_element_get_request_pad (gAVRM_handle->AVIMux, "video_00");
                lres = gst_pad_link (videoqueue_src_pad, avimux_video_sink_pad);
                g_assert (lres == GST_PAD_LINK_OK);
                gst_object_unref (videoqueue_src_pad);



		if (avrm_mode == AVRM_AUDIO_VIDEO || avrm_mode == AVRM_AUDIO_ONLY)
		{
			gst_bin_add_many(GST_BIN(gAVRM_handle->pipeline), gAVRM_handle->alsaAudSrc, /*gAVRM_handle->audioPerf,*/ /*gAVRM_handle->audioRate,*/ gAVRM_handle->audioQue, /*gAVRM_handle->audio_enc,*/gAVRM_handle->audio_capsfilter, NULL);
#ifdef FILESPLIT_APPSINK
			if(TRUE != gst_element_link_many(gAVRM_handle->alsaAudSrc, gAVRM_handle->audio_capsfilter, /*gAVRM_handle->audioPerf,*/ /*gAVRM_handle->audioRate,*/ /*gAVRM_handle->audio_enc,*/ gAVRM_handle->audioQue, NULL))
			{
				g_print("Failed to link audio plugins\n");
				nRetVal = AVRM_FAILURE;
				goto labelExit;
			}

			audioqueue_src_pad = gst_element_get_static_pad (gAVRM_handle->audioQue, "src");
			avimux_audio_sink_pad = gst_element_get_request_pad (gAVRM_handle->AVIMux, "audio_00");
			lres = gst_pad_link (audioqueue_src_pad, avimux_audio_sink_pad);
			g_assert (lres == GST_PAD_LINK_OK);
			gst_object_unref (audioqueue_src_pad);


			//set clocks
			//GstClock *sysClock = gst_system_clock_obtain();
			//GstClock *vidClock = gst_element_get_clock(gAVRM_handle->omx_videosrc);
			//GstClock *audClock = gst_element_get_clock(gAVRM_handle->alsaAudSrc);
			//gboolean result = gst_element_set_clock(gAVRM_handle->alsaAudSrc, sysClock);
			//gboolean result = gst_clock_set_master(audClock, vidClock);
			//printf("gst_element_set_master aud result: %d\n", result);
			//result = gst_clock_set_master(vidClock, sysClock);
			//printf("gst_element_set_master vid result: %d\n", result);
			
#else
			if(TRUE != gst_element_link_many(gAVRM_handle->alsaAudSrc, gAVRM_handle->audio_capsfilter, gAVRM_handle->audioQue, NULL))
			{
				g_print("Failed to link audio plugins\n");
				nRetVal = AVRM_FAILURE;
				goto labelExit;
			}
			GstPad *audioqueue_src_pad, *avimux_audio_sink_pad;

			audioqueue_src_pad = gst_element_get_static_pad (gAVRM_handle->audioQue, "src");
			avimux_audio_sink_pad = gst_element_get_request_pad (gAVRM_handle->AVIMux, "audio_00");
			lres = gst_pad_link (audioqueue_src_pad, avimux_audio_sink_pad);
			g_assert (lres == GST_PAD_LINK_OK);
			gst_object_unref (avimux_audio_sink_pad);
			gst_object_unref (audioqueue_src_pad);
#endif
		}
#ifdef FILESPLIT_APPSINK

		if(TRUE != gst_element_link(gAVRM_handle->AVIMux, gAVRM_handle->appsink))
		{

			g_print("Failed to link mux and filesink\n");
			nRetVal = AVRM_FAILURE;
			goto labelExit;

		}
#else
		if(TRUE != gst_element_link(gAVRM_handle->AVIMux, gAVRM_handle->FileSink))
		{

			g_print("Failed to link mux and filesink\n");
			nRetVal = AVRM_FAILURE;
			goto labelExit;

		}
#endif
#endif
	}  
/*	if (TRUE != gst_element_set_clock(gAVRM_handle->pipeline, gst_system_clock_obtain()))
	{
		g_print("gst_element_set_clock(GstSystemClock) did not work\n");
		nRetVal = AVRM_FAILURE;
		goto labelExit;
	}*/
	/*set file location*/
	/* create thread for playing the pipeline*/
	if(0 != pthread_create (&(gAVRM_handle->playing_thread), &detach_attr, (void*)play_pipeline, NULL))
	{

		g_print("Falied to create required pipeline thread\n");
		nRetVal = AVRM_FAILURE;
		goto labelExit;

	}
	usleep(1000);
	while((ismainloop_running = AVRM_Status()) == AVRM_FAILURE)
	{
		if(gAVRM_handle->bFailedToGoPlayState)
		{
			return AVRM_FAILURE; 
		}
		printf("\n Waiting to go into playing state");
		//usleep(10);
		sleep(1);
	}
labelExit:
	if(nRetVal == AVRM_FAILURE)
	{
		if(gAVRM_handle->pipeline)
		{
			printf("\n Unreffing pipeline from AVRM start");
			gst_object_unref (GST_OBJECT(gAVRM_handle->pipeline));
		}
	}
	return nRetVal;

}
static void error_cb (GstBus *bus, GstMessage *msg, gpointer *data) 
{
	GError *err = NULL;
	gchar *debug_info = NULL;
	gboolean result;
	gst_message_parse_error (msg, &err, &debug_info);
	g_print("error_cb\n");
	if(err)
	{
		g_print ("Error received from element %s: %s: %d from %s\n",
				GST_OBJECT_NAME (msg->src), err->message, err->code, msg->src->name );
	}
	g_clear_error (&err);
	g_free (debug_info);
	if (gAVRM_handle->main_loop && g_main_loop_is_running (gAVRM_handle->main_loop))
	{
		g_print ("Quit Main loop from error event \n");
		g_main_loop_quit(gAVRM_handle->main_loop);
	}
	else
	{
		g_print ("loop running status from error_cb %d loop Ref=%d\n",g_main_loop_is_running (gAVRM_handle->main_loop),gAVRM_handle->main_loop);
	}
}

static void eos_cb (GstBus *bus, GstMessage *msg, gpointer *data) 
{
		printf("\neos_cb called");
	if (gAVRM_handle->main_loop && g_main_loop_is_running (gAVRM_handle->main_loop))
	{
		printf("\nQuiting mainloop from eos_cb");
		g_main_loop_quit(gAVRM_handle->main_loop);
		
	}
	else                                                                                                                                                           
	{                                                                                                                                                                
		g_print ("loop running status from eos_cb %d loop Ref=%d\n",g_main_loop_is_running (gAVRM_handle->main_loop),gAVRM_handle->main_loop);
	}
}
/** PP comment: This callback gets invoked whenever resolution mismatch detected b/t driver and application
 * This will be trigger by videosrc gstreamer object
 */
static void Video_Res_mismatch(void)
{
	guint width,height;
	GstStateChangeReturn ret;
	g_print("*****************Got the signal for resolution mismatch ***********\n");
	g_object_get(G_OBJECT(gAVRM_handle->omx_videosrc),"width",&width,NULL);
	g_object_get(G_OBJECT(gAVRM_handle->omx_videosrc),"height",&height,NULL);
	gAVRM_handle->nWidth         = width;
	gAVRM_handle->nHeight        = height;

	g_print("Capture Width: %d height=%d\n",gAVRM_handle->nWidth, gAVRM_handle->nHeight);
        
	if ((gAVRM_handle->main_loop) && (g_main_loop_is_running (gAVRM_handle->main_loop)))
        {
		g_print ("Quit Main loop from mismatch event\n");
                g_main_loop_quit(gAVRM_handle->main_loop);
                gAVRM_handle->gbEosInitiated = TRUE;
        }
	else
	{
        	gAVRM_handle->bResMisMatch = TRUE;
                gAVRM_handle->gbEosInitiated = TRUE;
		g_print ("main loop not yet started but vr mis match found\n");  
	}
	
}


static gboolean busCall (GstBus *bus, GstMessage *msg, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;

	switch (GST_MESSAGE_TYPE (msg)) {
		
		case GST_MESSAGE_EOS:
			{
				//printf("End of stream\n");
				g_main_loop_quit (loop);
			}
			break;

		case GST_MESSAGE_ERROR:
			{
				gchar *debug;
				GError *error;

				gst_message_parse_error (msg, &error, &debug);
				g_free (debug);

				printf("Error: %s\n", error->message);
				g_error_free (error);

				g_main_loop_quit (loop);
				break;
			}
		case GST_MESSAGE_STREAM_STATUS:
			//printf ("GST_MESSAGE_STREAM_STATUS\n");
			break;
		case GST_MESSAGE_STATE_CHANGED:
			{
				GstState old_state, new_state, pending_state;
				gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
				//printf("\nPipeline state changed from %s to %s:\n",
				//		gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
			}
			break;
		default:
			break;
	}

	return TRUE;
}


static void *play_pipeline (void )
{
	GSource *bus_source;
	guint busWatchId;
	GstBus *bus;
	GstStateChangeReturn ret;

	prctl(PR_SET_NAME,"play_pipeline_thread",0,0,0);
#ifdef BUS_ORG
	gAVRM_handle->main_loop_cntxt = g_main_context_new();
	g_main_context_push_thread_default(gAVRM_handle->main_loop_cntxt);
	bus = gst_element_get_bus (gAVRM_handle->pipeline);
	bus_source = gst_bus_create_watch (bus);
	g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func, NULL, NULL);
	g_source_attach (bus_source, gAVRM_handle->main_loop_cntxt);
	g_source_unref (bus_source);
	//gst_debug_set_threshold_for_name("GST_EVENT",GST_LEVEL_DEBUG);
	g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, NULL);
	g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, NULL);

	gAVRM_handle->main_loop = g_main_loop_new(gAVRM_handle->main_loop_cntxt, FALSE);
	if(gAVRM_handle->main_loop == NULL)
	{
		g_print ("PP_WARNING::Failed to create main loop\r\n");
		goto labelExit;
	}
#else
	gAVRM_handle->main_loop = g_main_loop_new(NULL, FALSE);
	if(gAVRM_handle->main_loop == NULL)
	{
		g_print ("PP_WARNING::Failed to create main loop\r\n");
		goto labelExit;
	}
	/* we add a message handler */
	bus = gst_pipeline_get_bus (GST_PIPELINE (gAVRM_handle->pipeline));
	busWatchId = gst_bus_add_watch (bus, busCall, gAVRM_handle->main_loop);

#endif
        /*Setting filename based on current time*/
        //setFileName();
	ret = gst_element_set_state (gAVRM_handle->pipeline, GST_STATE_PLAYING);
	if (gst_element_get_state (gAVRM_handle->pipeline, NULL, NULL, (GST_SECOND * 10)) != GST_STATE_CHANGE_SUCCESS)
	{
		g_print ("PP_WARNING::Failed to go into PLAYING state\r\n");
		gAVRM_handle->bFailedToGoPlayState = TRUE;
                gAVRM_handle->gbEosInitiated = TRUE;
		goto labelExit;
	}
	if(gAVRM_handle->bResMisMatch == TRUE)
	{ 
		g_print("mis match found before main loop starts \n");
	}
	else
	{
		g_main_loop_run (gAVRM_handle->main_loop);
	}
#ifdef FILESPLIT_APPSINK
	printf("\n Unlinking elements");
	gst_element_unlink_many(gAVRM_handle->omx_videosrc, gAVRM_handle->clockoverlay,gAVRM_handle->omx_videoEnc, gAVRM_handle->videoqueue, NULL);
	gst_element_unlink_many(gAVRM_handle->alsaAudSrc, gAVRM_handle->audio_capsfilter, gAVRM_handle->audioQue, NULL);
	gst_element_release_request_pad(gAVRM_handle->AVIMux, avimux_video_sink_pad);
	gst_object_unref (avimux_video_sink_pad);
	gst_element_release_request_pad(gAVRM_handle->AVIMux, avimux_audio_sink_pad);
	gst_object_unref (avimux_audio_sink_pad);
	gst_element_unlink_many(gAVRM_handle->AVIMux, gAVRM_handle->appsink,NULL);
#endif

labelExit:

#ifdef BUS_ORG
	if(gAVRM_handle->main_loop_cntxt)
	{
		g_main_context_pop_thread_default(gAVRM_handle->main_loop_cntxt);
		g_main_context_unref (gAVRM_handle->main_loop_cntxt);
	}
#endif
	if(bus)
	{
		gst_object_unref(bus);
	}
	pthread_exit(0);
}

eAVRM_RETURN AVRM_Stop()
{
	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
	GstStateChangeReturn stateChange;

	if (eos){
		while((g_main_loop_is_running (gAVRM_handle->main_loop)))
		{
			sleep(1);
			printf("\n Already got EOS. Waiting to quit main loop");
		}
		return nRetVal;
	}
	eos = 1;
/*	while(thread_exit != 1)	
	{
		printf("\n Main thread : Waiting to close file");
		sleep(1);
	}*/
	while(file_closed != 1)	
	{
		printf("\n Main thread : Waiting to close file");
		sleep(1);
	}
        if(!gAVRM_handle->gbEosInitiated)
        {
                 g_print ("Sending eos from AVRM_stop in case of ctrl+c\n");
		 gst_element_send_event(gAVRM_handle->pipeline,gst_event_new_eos());
                 usleep(10);
        }
        while((g_main_loop_is_running (gAVRM_handle->main_loop)))
        {
		sleep(1);
		printf("\n Waiting to quit main loop");
        }

	if(gAVRM_handle->pipeline)
	{
		stateChange = gst_element_set_state (gAVRM_handle->pipeline, GST_STATE_NULL);
		printf("ret value of state change %d\n", stateChange);
	}
	if(gAVRM_handle->main_loop)
	{
		g_main_loop_unref (gAVRM_handle->main_loop);
	}
	if(gAVRM_handle->pipeline)
	{
		gst_object_unref (gAVRM_handle->pipeline);
	}

	/*if(gAVRM_handle->playing_thread != NULL)
	{
		pthread_cancel(gAVRM_handle->playing_thread);
	}*/
        printf ("Stopping avrm done \n");

	//printf ("\nReset numVidFrames, numAudSamples, numAudBlocksToDrop, numAudCompSkipCnt \n");
	numVidFrames = 0;
	numAudSamples = 0;

	return nRetVal;

}

eAVRM_RETURN AVRM_DeInit()
{
	eAVRM_RETURN nRetVal = AVRM_SUCCESS;

	if(gAVRM_handle)
	{
		free(gAVRM_handle);
		gAVRM_handle = NULL;
	}    
#ifdef FILESPLIT_APPSINK
	RingBufDestroy(ringBufHandle);
	sem_destroy(&ringBufReady);
#endif
        g_print ("Avrm deint done \n"); 
	return nRetVal;   
}

eAVRM_RETURN AVRM_Status()
{
	eAVRM_RETURN nRetVal = AVRM_SUCCESS;
	if(gAVRM_handle->bResMisMatch == TRUE)
	{
		g_print("Returning status as MISMATCH\n");
		nRetVal = AVRM_RESMISMATCH;
	}
	else if(g_main_is_running(gAVRM_handle->main_loop) == TRUE)
	{
		//printf("\n Main loop running");
		if(gAVRM_handle->nDuration != 0)
		{
			GstFormat format = GST_FORMAT_TIME;
			guint64 cur_pos, duration;
			if(gst_element_query_position(gAVRM_handle->pipeline, &format, &cur_pos)){
				if((cur_pos / 1000000) >= ((gAVRM_handle->nDuration * 1000)-10) && (!gAVRM_handle->gbEosInitiated))
				{
					//g_print("current time is near to specified time\n");
					//gst_element_send_event(gAVRM_handle->pipeline,gst_event_new_eos ());
                                        //gAVRM_handle->gbEosInitiated = TRUE;
				}
			}
			else
			{
				if(gAVRM_handle->avrm_mode != AVRM_VIDEO_ONLY)
				{
					g_print("Failed to get the current duration\n");
					nRetVal = AVRM_FAILURE;
				}
			}
		}
		else if(gAVRM_handle->nFsize_in_mb != 0)
		{
			//printf("\nFile size != 0");
			struct stat buf;
			gint ret = -1;
			ret = stat(gAVRM_handle->cFile, &buf);
			//printf("Checking status of file %s Size %d KB\n",gAVRM_handle->cFilePath,(buf.st_size/1024));
			if(ret == 0 )
			{
				if((buf.st_size/1024) > ((gAVRM_handle->nFsize_in_mb*1024)-300) && (!gAVRM_handle->gbEosInitiated))
				{
					//g_print("current size is near to specified size\n");
#if 0 
					gst_element_send_event(gAVRM_handle->pipeline,gst_event_new_eos ());
					gAVRM_handle->gbEosInitiated = TRUE;                                       
#else
#endif
				}
			}
			else
			{
				g_print("Failed to get the current file size\n");
				nRetVal = AVRM_FAILURE;
			}
		}
	}
	else
	{
		//printf("\n Returning failure from AVRM_Status");
		nRetVal = AVRM_FAILURE;    
	}
	return nRetVal;
}
