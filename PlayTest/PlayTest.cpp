// ConsoleApplication1.cpp : Defines the entry point for the console application.
//
#include "stdafx.h"
#include <windows.h>
#define _SDL_main_h
extern "C" 
{
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libswscale/swscale.h>
	#include <SDL.h>
	#include <SDL_thread.h>
}
#pragma comment(lib,"libavcodec.a")
#pragma comment(lib,"libavformat.a")
#pragma comment(lib,"libavdevice.a")
#pragma comment(lib,"libavfilter.a")
#pragma comment(lib,"libavutil.a")
#pragma comment(lib,"libswresample.a")
#pragma comment(lib,"libswscale.a")
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Secur32.lib")
#pragma comment (lib, "Sdl.lib")

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif


// compatibility with newer API
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

void SaveFrame(AVFrame *pFrame, int nFrameWidth, int nFrameHeight, int nFrameIndex) {
	FILE *pFile = NULL;
	char szFilename[32];
	int  y;

	// Open file
	sprintf_s(szFilename, 32, "frame%d.ppm", nFrameIndex);
	if(0!=fopen_s(&pFile, szFilename, "wb"))
	{
		return;
	}
	// Write header
	fprintf(pFile, "P6\n%d %d\n255\n", nFrameWidth, nFrameHeight);

	// Write pixel data
	for (y = 0; y < nFrameHeight; y++)
		fwrite(pFrame->data[0] + y*pFrame->linesize[0], 1, nFrameWidth * 3, pFile);

	// Close file
	fclose(pFile);
}

int DecodeVideoToFrames(const char *pszFileName, long nTotalFrames = -1)
{
	// Initalizing these to NULL prevents segfaults!
	AVFormatContext   *pFormatCtx = NULL;

	// Open media file
	// reads the file header and stores information about the file in AVFormatContext structure
	if (avformat_open_input(&pFormatCtx, pszFileName, NULL, NULL) != 0)
	{
		return -1; // Couldn't open file
	}


	// Retrieve stream information by populating pFormatCtx->streams with the proper information
	// 
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
	{
		return -1; // Couldn't find stream information
	}

	// Dump information about file onto standard error - very handy in terms of debugging
	av_dump_format(pFormatCtx, 0, pszFileName, 0);

	// Find the first video stream
	int nVideoStreamIndex = -1;
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) 
		{
			nVideoStreamIndex = i;
			break;
		}
	}

	if (nVideoStreamIndex == -1)
	{
		return -1; // Didn't find a video stream
	}

	// Get a pointer to the video stream's codec context
	AVStream * pStream = pFormatCtx->streams[nVideoStreamIndex];
	AVCodecContext *pSrcCodecCtx = pStream->codec;

	// Find the decoder for the video stream
	AVCodec *pCodec = NULL;
	pCodec = avcodec_find_decoder(pSrcCodecCtx->codec_id);
	if (pCodec == NULL) 
	{
		fprintf(stderr, "Unsupported codec!\n");
		return -1; // Codec not found
	}
	// we must not use the AVCodecContext from the video stream directly, we have to copy it to a new location 
	AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
	if (avcodec_copy_context(pCodecCtx, pSrcCodecCtx) != 0) {
		fprintf(stderr, "Couldn't copy codec context");
		return -1; // Error copying codec context
	}

	// Open codec
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
	{
		return -1; // Could not open codec
	}

	// Allocate video frame
	AVFrame *pFrame = av_frame_alloc();
	AVFrame *pFrameRGB = av_frame_alloc();
	// Allocate an AVFrame structure
	if (pFrameRGB == NULL)
	{
		return -1;
	}
	// Determine required buffer size and allocate buffer
	int numBytes = avpicture_get_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
	uint8_t *pBuffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
	// Assign appropriate parts of buffer to image planes in pFrameRGB, 
	// Note that pFrameRGB is an AVFrame, but AVFrame is a superset of AVPicture
	avpicture_fill((AVPicture *)pFrameRGB, pBuffer, AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);


	// initialize SWS context for software scaling
	struct SwsContext *sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGB24, SWS_BILINEAR,NULL,NULL,NULL);
	AVPacket  packet;
	int nFrameIndex = 0;
	while (av_read_frame(pFormatCtx, &packet) >= 0) 
	{
		int frameFinished;
		// Read frames and save first five frames to disk
		// Is this a packet from the video stream?
		if (packet.stream_index == nVideoStreamIndex) {
			// Decode video frame
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
			// Did we get a video frame?
			if (frameFinished) {
				// Convert the image from its native format to RGB
				sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data,pFrame->linesize, 0, pCodecCtx->height,pFrameRGB->data, pFrameRGB->linesize);
				// Save the frame to disk
				if (nTotalFrames>0 && ++nFrameIndex <= nTotalFrames)
				{
					SaveFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height, nFrameIndex);
				}
			}
		}
		// Free the packet that was allocated by av_read_frame
		av_free_packet(&packet);
	}

	// Free the RGB image
	av_free(pBuffer);
	av_frame_free(&pFrameRGB);

	// Free the YUV frame
	av_frame_free(&pFrame);

	// Close the codecs
	avcodec_close(pCodecCtx);
	avcodec_close(pSrcCodecCtx);

	// Close the video file
	avformat_close_input(&pFormatCtx);
	return 0;
}


int PlayVideoFrames(const char *pszFileName)
{
	// Initalizing these to NULL prevents segfaults!
	AVFormatContext   *pFormatCtx = NULL;
	// Open media file
	// reads the file header and stores information about the file in AVFormatContext structure
	if (avformat_open_input(&pFormatCtx, pszFileName, NULL, NULL) != 0)
	{
		return -1; // Couldn't open file
	}


	// Retrieve stream information by populating pFormatCtx->streams with the proper information
	// 
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
	{
		return -1; // Couldn't find stream information
	}

	// Find the first video stream
	int nVideoStreamIndex = -1;
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			nVideoStreamIndex = i;
			break;
		}
	}

	if (nVideoStreamIndex == -1)
	{
		return -1; // Didn't find a video stream
	}

	// Get a pointer to the video stream's codec context
	AVStream * pStream = pFormatCtx->streams[nVideoStreamIndex];
	AVCodecContext *pSrcCodecCtx = pStream->codec;

	// Find the decoder for the video stream
	AVCodec *pCodec = NULL;
	pCodec = avcodec_find_decoder(pSrcCodecCtx->codec_id);
	if (pCodec == NULL)
	{
		fprintf(stderr, "Unsupported codec!\n");
		return -1; // Codec not found
	}
	// we must not use the AVCodecContext from the video stream directly, we have to copy it to a new location 
	AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
	if (avcodec_copy_context(pCodecCtx, pSrcCodecCtx) != 0) {
		fprintf(stderr, "Couldn't copy codec context");
		return -1; // Error copying codec context
	}

	// Open codec
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
	{
		return -1; // Could not open codec
	}

	// Allocate video frame
	AVFrame *pFrame = av_frame_alloc();
	// Make SDL screen
	SDL_Surface *pSdlSurface = SDL_SetVideoMode(pCodecCtx->width,pCodecCtx->height,0,0);
	if (NULL == pSdlSurface)
	{
		return -1;
	}
	// allocate a place to put YUV image on that screen
	SDL_Overlay *pSdlOverlay = SDL_CreateYUVOverlay(pCodecCtx->width, pCodecCtx->height, SDL_YV12_OVERLAY, pSdlSurface);

	// initialize SWS context for software scaling
	struct SwsContext *sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

	AVPacket  packet;
	int nFrameIndex = 0;
	while (av_read_frame(pFormatCtx, &packet) >= 0)
	{
		int frameFinished;
		// Read frames and save first five frames to disk
		// Is this a packet from the video stream?
		if (packet.stream_index == nVideoStreamIndex) {
			// Decode video frame
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
			// Did we get a video frame?
			if (frameFinished)
			{
				Sleep(1000/25);
				SDL_LockYUVOverlay(pSdlOverlay); 
				AVPicture pict;
				pict.data[0] = pSdlOverlay->pixels[0];
				pict.data[1] = pSdlOverlay->pixels[2];
				pict.data[2] = pSdlOverlay->pixels[1];

				pict.linesize[0] = pSdlOverlay->pitches[0];
				pict.linesize[1] = pSdlOverlay->pitches[2];
				pict.linesize[2] = pSdlOverlay->pitches[1];

				// Convert the image into YUV format that SDL uses
				sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pict.data, pict.linesize);
				SDL_UnlockYUVOverlay(pSdlOverlay);

				SDL_Rect  rect;
				rect.x = 0;
				rect.y = 0;
				rect.w = pCodecCtx->width;
				rect.h = pCodecCtx->height;
				SDL_DisplayYUVOverlay(pSdlOverlay, &rect);
			}
		}
		// Free the packet that was allocated by av_read_frame
		av_free_packet(&packet);
		SDL_Event  event;
		SDL_PollEvent(&event);
		switch (event.type)
		{
		case SDL_QUIT:
			SDL_Quit();
			exit(0);
			break;
		default:
			break;
		}
	}

	// Free the YUV frame
	av_frame_free(&pFrame);

	// Close the codecs
	avcodec_close(pCodecCtx);
	avcodec_close(pSrcCodecCtx);

	// Close the video file
	avformat_close_input(&pFormatCtx);
	return 0;
}
int main(int argc, char *argv[]) {

	// Register all formats and codecs
	av_register_all();
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}

	const char *pszFileName = "G:\\NswSamples\\Video\\Video2.WMV";
	DecodeVideoToFrames(pszFileName, 5);
	PlayVideoFrames("G:\\NewswireSampleData\\Video1.WMV");
//	PlayVideoFrames(pszFileName);


/*
	ShowVideoFileProperties("G:\\NewswireSampleData\\Sample85.aac");
	ShowVideoFileProperties("G:\\NewswireSampleData\\testmp3.mp3");
	ShowVideoFileProperties("G:\\NewswireSampleData\\wavetest.wav");
	ShowVideoFileProperties("G:\\NewswireSampleData\\Luci-Test_20150413-165312.wav");
	ShowVideoFileProperties("G:\\NewswireSampleData\\VRB_Test_RadioTop.mp3");
	ShowVideoFileProperties("G:\\NewswireSampleData\\9LKYNO.wav");
*/
	return 0;
}
