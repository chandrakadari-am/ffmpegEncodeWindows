#pragma once
//#include "glm/mat3x3.hpp"
//#include "glm/vec2.hpp"
#include <mutex>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include "sc_encoder_if.h"
//#include "shared/desktop/media/media_common.int.hpp"
//#include "shared/desktop/media/media_reader.int.hpp"


namespace md {

    struct video_view_parameters_t;
    typedef std::shared_ptr<video_view_parameters_t> video_view_parameters_pt;
    struct video_view_parameters_t
    {
        video_view_parameters_t() {};
        ~video_view_parameters_t() {};

        video_view_parameters_t(video_view_parameters_t const& other) = delete;
        void operator=(video_view_parameters_t const&) = delete;

        AVCodecParameters* codec_params = nullptr;
        AVBufferRef* hw_device_ctx = nullptr;
        AVBufferRef* hw_frames_ctx = nullptr;
        AVRational time_base{ 0, 0 };
        //glm::mat3x3 display_matrix = glm::mat3x3(1.0f);
        //glm::vec2 display_size;
    };

    struct stream_t
    {
        virtual ~stream_t() {};

        int stream_idx = 0;                     //in AVFormatContext::streams and in media_file_reader2_impl_t::streams

        AVCodec const* codec = nullptr;
        AVCodecContext* codec_cx = nullptr;
    };

    struct video_stream_t : public stream_t
    {
        video_stream_t() {};

        //frame_pt                        frame_bb;       // Frame backbuffer to avoid a problem when 'this one' failed to decode. Especially valuable for the very last frame in a video
        video_view_parameters_pt        vv_parameters;

        //spsc_queue_pt<frame_and_packets_t> video_frame_queue;
    };

    class video_file_player_t
    {
    };
    struct packet_t
    {
        ~packet_t() {};

        AVPacket* packet = nullptr;
        bool                has_data = false;
        bool                is_eof = false;
    };

    struct frame_t
    {
        ~frame_t() {};

        AVFrame* frame = nullptr;
        bool has_data = false;
    };

    struct packet_t;
    typedef std::shared_ptr<packet_t> packet_pt;

    struct frame_t;
    typedef std::shared_ptr<frame_t> frame_pt;

};

using namespace std;
struct video_packet_sink_it; //see video_packet_sink.hpp

enum AM_SC_ENCODING_TYPES {
    AM_SC_HW_ENCODING = 1,
    AM_SC_SW_ENCODING,
    AM_SC_UNKNOWN_ENCODING
};

typedef struct {
    int32_t width;
    int32_t height;
    int32_t bitrate;
    int32_t framerate;
    int32_t codecUIIndex;
    enum AVPixelFormat pixfmt;
    enum AVCodecID codecId;
    uint32_t qualityMode;       // set codec bitrate based on quality mode
    const AVCodec *selCodec;
    enum AM_SC_ENCODING_TYPES encodingType;
    uint32_t deviceType;
    char hwDeviceTypeName[20];
} encConfig;

class ScreenCaptureEncoder {
public:

    static ScreenCaptureEncoder* getScEncInstance() {
        if (scEncInstPtr == nullptr) {
            lock_guard<mutex> lock(scEncMutex);
            if (scEncInstPtr == nullptr) {
                scEncInstPtr = new ScreenCaptureEncoder();
                scEncFFmpegInit();
            }
        }
        return scEncInstPtr;
    }
    
    void InitHWEncoder(const encConfig &enccfg);
    void InitSWEncoder(const encConfig &enccfg);
    void InitEncoder(const encIfConfig *cfg);
    void EncodeFramesOnHW(void *pixelBuffer, bool isTransmissionStarted);
    void EncodeFramesOnSW(uint8_t *pixelBuffer, bool isTransmissionStarted);
    void FlushEncoder();
    void CloseEncoder();
    void ReleaseEncResource();
    void SetPlayerInstance(md::video_file_player_t *player);

private:
    static ScreenCaptureEncoder *scEncInstPtr;
    static std::mutex scEncMutex;
    ScreenCaptureEncoder() {};
    ~ScreenCaptureEncoder() {};
    ScreenCaptureEncoder(const ScreenCaptureEncoder&) = delete;
    ScreenCaptureEncoder& operator=(const ScreenCaptureEncoder&) = delete;
    void SendEncodedPacketToPacketizer(AVPacket *pkt, AVFrame *frame, bool has_data);
    static void scEncFFmpegInit(void) {
        avformat_network_init();
    }
    
    bool m_isEncConfigured;
    int32_t m_frameIndex;
    encConfig m_enccfg;
    
    AVFormatContext *m_formatCtx;
    AVStream *m_stream;
    AVFrame *m_frame;
    AVCodecContext *m_codecCtx;
    AVPacket *m_packet;
    AVBufferRef *m_hw_device_ctx;
    AVBufferRef *m_hw_frames_ctx;
    
    md::video_file_player_t  *m_video_display_player;
    md::frame_pt m_video_frame;
    md::packet_pt m_enc_packet;
    md::video_stream_t m_video_stream;
    std::mutex m_enc_mutex;
    
};


