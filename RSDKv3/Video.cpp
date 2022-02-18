#include "RetroEngine.hpp"

// TODO: all of the functions here have been stubbed for 3DS.
// Re-implement everything later.

int currentVideoFrame = 0;
int videoFrameCount   = 0;
int videoWidth        = 0;
int videoHeight       = 0;
float videoAR         = 0;

THEORAPLAY_Decoder *videoDecoder;
const THEORAPLAY_VideoFrame *videoVidData;
THEORAPLAY_Io callbacks;

byte videoData    = 0;
int videoFilePos  = 0;
bool videoPlaying = 0;
int vidFrameMS    = 0;
int vidBaseticks  = 0;

bool videoSkipped = false;

static long videoRead(THEORAPLAY_Io *io, void *buf, long buflen)
{
    # if RETRO_USING_SDL
    FileIO *file    = (FileIO *)io->userdata;
    const size_t br = fRead(buf, 1, buflen * sizeof(byte), file);
    if (br == 0)
        return -1;
    return (int)br;
    #else
    return -1;
    #endif
} // IoFopenRead

static void videoClose(THEORAPLAY_Io *io)
{
#if RETRO_USING_SDL2 || RETRO_USING_SDL1
    FileIO *file = (FileIO *)io->userdata;
    fClose(file);
#endif
}

void PlayVideoFile(char *filePath)
{
    char pathBuffer[0x100];
    int len = StrLength(filePath);

    if (StrComp(filePath + ((size_t)len - 2), "us")) {
        filePath[len - 2] = 0;
    }

    StrCopy(pathBuffer, "videos/");
    StrAdd(pathBuffer, filePath);

    if (GetGlobalVariableByName("Options.Soundtrack")) {
        StrAdd(pathBuffer, ".us.ogv");
    } else {
        StrAdd(pathBuffer, ".ogv");
    }
    
    bool addPath = true;
    // Fixes ".ani" ".Ani" bug and any other case differences
    char pathLower[0x100];
    memset(pathLower, 0, sizeof(char) * 0x100);
    for (int c = 0; c < strlen(pathBuffer); ++c) {
        pathLower[c] = tolower(pathBuffer[c]);
    }

#if RETRO_USE_MOD_LOADER
    for (int m = 0; m < modList.size(); ++m) {
        if (modList[m].active) {
            std::map<std::string, std::string>::const_iterator iter = modList[m].fileMap.find(pathLower);
            if (iter != modList[m].fileMap.cend()) {
                StrCopy(pathBuffer, iter->second.c_str());
                Engine.forceFolder   = true;
                Engine.usingDataFile = false;
                addPath = false;
                break;
            }
        }
    }
#endif
    
    char filepath[0x100];
    if (addPath) {
#if RETRO_PLATFORM == RETRO_UWP
        static char resourcePath[256] = { 0 };

        if (strlen(resourcePath) == 0) {
            auto folder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder();
            auto path   = to_string(folder.Path());

            std::copy(path.begin(), path.end(), resourcePath);
        }

        sprintf(filepath, "%s/%s", resourcePath, pathBuffer);
#elif RETRO_PLATFORM == RETRO_OSX || RETRO_PLATFORM == RETRO_ANDROID
        sprintf(filepath, "%s/%s", gamePath, pathBuffer);
#else
        sprintf(filepath, "%s%s", BASE_PATH, pathBuffer);
#endif
    }
    else {
        sprintf(filepath, "%s", pathBuffer);
    }

    FileIO *file = fOpen(filepath, "rb");
#if RETRO_PLATFORM == RETRO_3DS
    if (file) {
	CloseFile();
        PlayVideo(filepath);
	videoPlaying = true;
        Engine.gameMode = ENGINE_VIDEOWAIT;
	videoSkipped = false;
    } else {
        printLog("could not find %s, ignoring", filepath);
    }
#elif RETRO_USING_SDL
       if (file) {
        printLog("Loaded File '%s'!", filepath);

        callbacks.read     = videoRead;
        callbacks.close    = videoClose;
        callbacks.userdata = (void *)file;
#if RETRO_USING_SDL2
        videoDecoder = THEORAPLAY_startDecode(&callbacks, /*FPS*/ 30, THEORAPLAY_VIDFMT_IYUV, GetGlobalVariableByName("Options.Soundtrack") ? 1 : 0);
#endif

        //TODO: does SDL1.2 support YUV?
#if RETRO_USING_SDL1
        videoDecoder = THEORAPLAY_startDecode(&callbacks, /*FPS*/ 30, THEORAPLAY_VIDFMT_RGBA, GetGlobalVariableByName("Options.Soundtrack") ? 1 : 0);
#endif


        if (!videoDecoder) {
            printLog("Video Decoder Error!");
            return;
        }
        while (!videoVidData) {
            if (!videoVidData)
                videoVidData = THEORAPLAY_getVideo(videoDecoder);
        }
        if (!videoVidData) {
            printLog("Video Error!");
            return;
        }

        videoWidth  = videoVidData->width;
        videoHeight = videoVidData->height;
        // commit video Aspect Ratio.
        videoAR = float(videoWidth) / float(videoHeight);

        SetupVideoBuffer(videoWidth, videoHeight);
        vidBaseticks = SDL_GetTicks();
        vidFrameMS   = (videoVidData->fps == 0.0) ? 0 : ((Uint32)(1000.0 / videoVidData->fps));
        videoPlaying = true;
        trackID      = TRACK_COUNT - 1;

        videoSkipped    = false;
        Engine.gameMode = ENGINE_VIDEOWAIT;
    }
    else {
        printLog("Couldn't find file '%s'!", filepath);
    }
#endif
}

void UpdateVideoFrame()
{
#if RETRO_PLATFORM == RETRO_3DS
    if (videodone) {
         StopVideoPlayback();	
    }
#elif RETRO_USING_SDL2 || RETRO_USING_SDL1
    if (videoPlaying) {
        if (videoFrameCount > currentVideoFrame) {
            GFXSurface *surface = &gfxSurface[videoData];
            byte fileBuffer      = 0;
            byte fileBuffer2      = 0;
            FileRead(&fileBuffer, 1);
            videoFilePos += fileBuffer;
            FileRead(&fileBuffer, 1);
            videoFilePos += fileBuffer << 8;
            FileRead(&fileBuffer, 1);
            videoFilePos += fileBuffer << 16;
            FileRead(&fileBuffer, 1);
            videoFilePos += fileBuffer << 24;

            byte clr[3];
            for (int i = 0; i < 0x80; ++i) {
                FileRead(&clr, 3);
                activePalette32[i].r = clr[0];
                activePalette32[i].g = clr[1];
                activePalette32[i].b = clr[2];
                activePalette[i]     = ((ushort)(clr[0] >> 3) << 11) | 32 * (clr[1] >> 2) | (clr[2] >> 3);
            }

            FileRead(&fileBuffer, 1);
            while (fileBuffer != ',') FileRead(&fileBuffer, 1); // gif image start identifier

            FileRead(&fileBuffer2, 2); // IMAGE LEFT
            FileRead(&fileBuffer2, 2); // IMAGE TOP
            FileRead(&fileBuffer2, 2); // IMAGE WIDTH
            FileRead(&fileBuffer2, 2); // IMAGE HEIGHT
            FileRead(&fileBuffer, 1); // PaletteType
            bool interlaced = (fileBuffer & 0x40) >> 6;
            if (fileBuffer >> 7 == 1) {
                int c = 0x80;
                do {
                    ++c;
                    FileRead(&fileBuffer, 3);
                } while (c != 0x100);
            }
            ReadGifPictureData(surface->width, surface->height, interlaced, graphicData, surface->dataPosition);

            SetFilePosition(videoFilePos);
            ++currentVideoFrame;
        }
        else {
            videoPlaying = 0;
            CloseFile();
        }
    }
#endif
}

int ProcessVideo()
{
    if (videoPlaying) {
        CheckKeyPress(&keyPress, 0x10);

        if (videoSkipped && fadeMode < 0xFF) {
            fadeMode += 8;
        }

        if (keyPress.A) {
            if (!videoSkipped)
                fadeMode = 0;

            videoSkipped = true;
        }

#if RETRO_PLATFORM == RETRO_3DS
        if (videoSkipped || videodone) {
            StopVideoPlayback();

	    return 1;
	}
#else
        if (!THEORAPLAY_isDecoding(videoDecoder) || (videoSkipped && fadeMode >= 0xFF)) {
            StopVideoPlayback();

            return 1; // video finished
        }
#endif

        // Don't pause or it'll go wild
        if (videoPlaying) {
#if RETRO_PLATFORM != RETRO_3DS
            const Uint32 now = (SDL_GetTicks() - vidBaseticks);

            if (!videoVidData)
                videoVidData = THEORAPLAY_getVideo(videoDecoder);

            // Play video frames when it's time.
            if (videoVidData && (videoVidData->playms <= now)) {
                if (vidFrameMS && ((now - videoVidData->playms) >= vidFrameMS)) {

                    // Skip frames to catch up, but keep track of the last one+
                    //  in case we catch up to a series of dupe frames, which
                    //  means we'd have to draw that final frame and then wait for
                    //  more.

                    const THEORAPLAY_VideoFrame *last = videoVidData;
                    while ((videoVidData = THEORAPLAY_getVideo(videoDecoder)) != NULL) {
                        THEORAPLAY_freeVideo(last);
                        last = videoVidData;
                        if ((now - videoVidData->playms) < vidFrameMS)
                            break;
                    }

                    if (!videoVidData)
                        videoVidData = last;
                }

                // do nothing; we're far behind and out of options.
                if (!videoVidData) {
                    // video lagging uh oh
                }

                int half_w     = videoVidData->width / 2;
                const Uint8 *y = (const Uint8 *)videoVidData->pixels;
                const Uint8 *u = y + (videoVidData->width * videoVidData->height);
                const Uint8 *v = u + (half_w * (videoVidData->height / 2));

#if RETRO_USING_SDL2
#if RETRO_SOFTWARE_RENDER
                SDL_UpdateYUVTexture(Engine.videoBuffer, NULL, y, videoVidData->width, u, half_w, v, half_w);
#endif
#endif
#if RETRO_USING_SDL1
#if RETRO_SOFTWARE_RENDER
                uint *videoFrameBuffer = (uint *)Engine.videoBuffer->pixels;
                memcpy(videoFrameBuffer, videoVidData->pixels, videoVidData->width * videoVidData->height * sizeof(uint));
#endif
#endif

                THEORAPLAY_freeVideo(videoVidData);
                videoVidData = NULL;
            }
#endif

            return 2; // its playing as expected
        }
    }

    return 0; // its not even initialised
}

void StopVideoPlayback()
{
#if RETRO_PLATFORM == RETRO_3DS
    if (videoPlaying) {
        CloseVideo();
        videoPlaying = false;
    }
    if (Engine.gameMode != ENGINE_EXITGAME)
        Engine.gameMode = ENGINE_MAINGAME;
#elif RETRO_USING_SDL2 || RETRO_USING_SDL1
    if (videoPlaying) {
        // `videoPlaying` and `videoDecoder` are read by
        // the audio thread, so lock it to prevent a race
        // condition that results in invalid memory accesses.
        SDL_LockAudio();

        if (videoSkipped && fadeMode >= 0xFF)
            fadeMode = 0;

        if (videoVidData) {
            THEORAPLAY_freeVideo(videoVidData);
            videoVidData = NULL;
        }
        if (videoDecoder) {
            THEORAPLAY_stopDecode(videoDecoder);
            videoDecoder = NULL;
        }

        CloseVideoBuffer();
        videoPlaying = false;

        SDL_UnlockAudio();
    }
#endif
}

void SetupVideoBuffer(int width, int height)
{
#if RETRO_SOFTWARE_RENDER
#if RETRO_USING_SDL1
    Engine.videoBuffer = SDL_CreateRGBSurface(0, width, height, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
#endif
#if RETRO_USING_SDL2
    Engine.videoBuffer = SDL_CreateTexture(Engine.renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, width, height);
#endif

    if (!Engine.videoBuffer)
        printLog("Failed to create video buffer!");
#endif
}

void CloseVideoBuffer()
{

#if RETRO_SOFTWARE_RENDER && RETRO_PLATFORM != RETRO_3DS
    if (videoPlaying) {
#if RETRO_USING_SDL1
        SDL_FreeSurface(Engine.videoBuffer);
#endif
#if RETRO_USING_SDL2
        SDL_DestroyTexture(Engine.videoBuffer);
#endif
        Engine.videoBuffer = nullptr;
    }
#endif
}
