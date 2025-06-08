#pragma once

//#include "shared/desktop/media/video_file_player.hpp"

enum SC_OS_VERSION {
    AM_SC_OS_VERSION_WINDOWS = 0,
    AM_SC_OS_VERSION_MAC
};

typedef struct {
    int32_t width;
    int32_t height;
    int32_t bitrate;
    double framerate;
    uint32_t codecIndex;
    uint32_t qualityModeIndex;
    uint32_t deviceType;
    uint32_t osVersion;
    char hwDeviceTypeName[20];
    void* vaDisplay;
} encIfConfig;

extern void ScEncoderConfigIF(const encIfConfig *cfg);
extern void ScEncodeFrames(void *pixelBuffer, bool isTransmissionStarted);
extern void ScEncodeFlush(void);
extern void ScEncodeClose(void);
extern void ScReleaseResource(void);
//extern void ScSetPlayer(md::video_file_player_t *player);
