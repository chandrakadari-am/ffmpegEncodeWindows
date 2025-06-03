
#include <iostream>
#include "sc_encoder.h"
#include "lgr.hpp"
//#include "shared/desktop/media/media_common.int.hpp"
struct resourceContext_t;
ScreenCaptureEncoder* ScreenCaptureEncoder::scEncInstPtr = nullptr;
mutex ScreenCaptureEncoder::scEncMutex;


void ScEncoderConfigIF(const encIfConfig *cfg) {
    ScreenCaptureEncoder* scEncObj = ScreenCaptureEncoder::getScEncInstance();
    scEncObj->InitEncoder(cfg);
}

void ScEncodeFrames(void *pixelBuffer, bool isTransmissionStarted) {
    ScreenCaptureEncoder* scEncObj = ScreenCaptureEncoder::getScEncInstance();
    scEncObj->EncodeFramesOnHW(pixelBuffer, isTransmissionStarted);
}

void ScEncodeFlush(void) {
    ScreenCaptureEncoder* scEncObj = ScreenCaptureEncoder::getScEncInstance();
    scEncObj->FlushEncoder();
}

void ScEncodeClose(void) {
    ScreenCaptureEncoder* scEncObj = ScreenCaptureEncoder::getScEncInstance();
    scEncObj->FlushEncoder();
    scEncObj->CloseEncoder();
}

void ScReleaseResource(void) {
    ScreenCaptureEncoder* scEncObj = ScreenCaptureEncoder::getScEncInstance();
    scEncObj->ReleaseEncResource();
}
/*
void ScSetPlayer(md::video_file_player_t *player) {
    ScreenCaptureEncoder* scEncObj = ScreenCaptureEncoder::getScEncInstance();
    scEncObj->SetPlayerInstance(player);
}
*/
void ScreenCaptureEncoder::SetPlayerInstance(md::video_file_player_t *player) {
    m_video_display_player = player;
}

void ScreenCaptureEncoder::InitHWEncoder(const encConfig &enccfg) {
    //av_log_set_level(AV_LOG_DEBUG);
    
    int ret;
    m_frameIndex = 0;

    if (!m_video_display_player) {
        //return;
    }

    enum AVCodecID codecId = enccfg.codecId;

    enum AVHWDeviceType device_type = av_hwdevice_find_type_by_name(enccfg.hwDeviceTypeName);
    if (device_type != enccfg.deviceType) {
        lgr_err(lgra_video, "[FAIL] FFMPEG Hardware device not found");
        return;
    }
    lgr_msg(lgra_video, "Found HW device");
    const AVCodec *codec = enccfg.selCodec;
    lgr_msg(lgra_video, "CODEC: %s %s", codec->name, codec->long_name);

    ret = av_hwdevice_ctx_create(&m_hw_device_ctx, device_type, NULL, NULL, 0);
    if (ret < 0) {
        lgr_err(lgra_video, "Failed to create videotoolbox HW context");
    }
    
    // configure a hardware frame context
    m_hw_frames_ctx = av_hwframe_ctx_alloc(m_hw_device_ctx);
    if (!m_hw_frames_ctx) {
        lgr_err(lgra_video, "[FAIL] FFMPEG Failed to allocate hardware frames context");
    }
    lgr_msg(lgra_video, "[ OK ] FFMPEG Successfully allocated hardware frames context");
    
    // Configure the frame context parameters
    AVHWFramesContext *frames_ctx = (AVHWFramesContext*)m_hw_frames_ctx->data;
    frames_ctx->format = m_enccfg.pixfmt;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;
    frames_ctx->width = enccfg.width;
    frames_ctx->height = enccfg.height;
    
    ret = av_hwframe_ctx_init((m_hw_frames_ctx));
    if (ret < 0) {
        lgr_err(lgra_video, "[FAIL] FFMPEG Failed to Initialize HW frames context");
        av_buffer_unref(&m_hw_frames_ctx);
        return;
    }
    lgr_msg(lgra_video, "[ OK ] FFMPEG Successfully hardware frames context initialized");
    
    const char* filename = "C:\\proj\\prototype\\ScreenCaptureDX12VaApiShare\\ffmpegEncodeWindows\\output.mp4";
    avformat_alloc_output_context2(&m_formatCtx, NULL, "mp4", filename);
    m_stream = avformat_new_stream(m_formatCtx, nullptr);
    m_stream->time_base = av_d2q(1.0 / enccfg.framerate, 60);
    m_stream->id = (int32_t)(m_formatCtx->nb_streams - 1);

    m_codecCtx = avcodec_alloc_context3(codec);
    m_codecCtx->width = enccfg.width;
    m_codecCtx->height = enccfg.height;
    m_codecCtx->bit_rate = enccfg.bitrate;
    m_codecCtx->time_base.num = 1;
    m_codecCtx->time_base.den = enccfg.framerate;
    m_codecCtx->gop_size = 60;
    m_codecCtx->pix_fmt = enccfg.pixfmt;
    m_codecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    m_codecCtx->color_range = AVCOL_RANGE_MPEG;
    m_codecCtx->codec_id = codecId;
    m_codecCtx->hw_frames_ctx = av_buffer_ref(m_hw_frames_ctx);
    m_codecCtx->hw_device_ctx = av_buffer_ref(m_hw_device_ctx);
    ret = av_hwdevice_ctx_init(m_codecCtx->hw_device_ctx);
    if (ret < 0)
    {
        lgr_err(lgra_video, "[FAIL] FFMPEG Failed to initialize HW Device Context");
        av_buffer_unref(&m_hw_frames_ctx);
        return;
    }
    lgr_msg(lgra_video, "[ OK ] FFMPEG Successfully initialized HW Device Context");
    
    ret = avcodec_open2(m_codecCtx, codec, NULL);
    if (ret < 0)
    {
        lgr_err(lgra_video, "[FAIL] FFMPEG Failed to Open Codec");
    }
    lgr_msg(lgra_video, "[ OK ] FFMPEG Successfully Opened Codec");
    
    if (m_video_frame) {
        m_video_frame.reset();
    }
    m_video_frame = std::make_shared<md::frame_t>();
    m_video_frame->frame = av_frame_alloc();
    m_frame = av_frame_alloc();
    m_frame->format = m_codecCtx->pix_fmt;
    m_frame->width = m_codecCtx->width;
    m_frame->height = m_codecCtx->height;
    //m_frame->hw_frames_ctx = av_buffer_ref(m_hw_frames_ctx);
    
    // allocate hardware frame
    ret = av_hwframe_get_buffer(m_hw_frames_ctx, m_frame, 0);
    if (ret < 0) {
        av_frame_free(&m_frame);
        av_buffer_unref(&m_hw_frames_ctx);
        lgr_err(lgra_video, "[FAIL] FFMPEG Failed to get hardware frame buffer");
        return;
    }

    ret = avcodec_parameters_from_context(m_stream->codecpar, m_codecCtx);
    if (ret < 0)
    {
        lgr_err(lgra_video, "[FAIL] FFMPEG Failed to Copy Codec parameters from context");
    }

    if (1) { // FILE Write ONLY
        ret = avio_open(&m_formatCtx->pb, filename, AVIO_FLAG_WRITE);
        if (ret != 0) {
            std::cout << "FAILED to Open avio filename" << std::endl;
        }
        ret = avformat_write_header(m_formatCtx, NULL);
        if (ret < 0) {
            std::cout << "Failed to write header" << std::endl;
        }
    }
    m_enc_packet = std::make_shared<md::packet_t>();
    m_enc_packet->packet = av_packet_alloc();
    m_packet = av_packet_alloc();
    m_packet->data = NULL;
    m_packet->size = 0;

    /*
    m_video_stream.codec = codec;
    m_video_stream.codec_cx = m_codecCtx;
    m_video_stream.stream_idx = m_stream->index;
    m_video_stream.codec_cx->hw_device_ctx = m_codecCtx->hw_device_ctx;
    m_video_stream.vv_parameters->hw_device_ctx = m_codecCtx->hw_device_ctx;
    m_video_stream.vv_parameters->time_base = m_codecCtx->time_base;
    //m_video_stream.vv_parameters->display_size = glm::vec2(m_codecCtx->width, m_codecCtx->height);
    m_video_stream.vv_parameters->codec_params = nullptr;
    m_video_stream.vv_parameters->hw_frames_ctx = nullptr;
    //m_video_display_player->open_display_resources((void*)&m_video_stream, (void*)m_formatCtx);
    */
    lgr_msg(lgra_video, "[ OK ] FFMPEG HW Encoder Configuration Done");
    m_isEncConfigured = true;
}

void ScreenCaptureEncoder::InitSWEncoder (const encConfig &enccfg) {
}

void ScreenCaptureEncoder::InitEncoder(const encIfConfig *cfg) {
    const char *hw_codec_names_mac[] = {"h264_videotoolbox", "hevc_videotoolbox", "NULL"};
    const char* hw_codec_names_win[] = { "h264_vaapi", "hevc_vaapi", "NULL" };
    char* hw_codec_name;
    const char *sw_codec_names[] = { "h264", "h265", "NULL"};
    const AVCodecID hw_codec_ids[] = {AV_CODEC_ID_H264, AV_CODEC_ID_H265, (AVCodecID)0};
    const AVCodecID sw_codec_ids[] = {AV_CODEC_ID_H264, AV_CODEC_ID_H265, (AVCodecID)0};
    const AVCodec *selCodec = NULL;
    
    if (m_isEncConfigured) {
        CloseEncoder();
    }
    m_enccfg.width = cfg->width;
    m_enccfg.height = cfg->height;
    m_enccfg.bitrate = cfg->bitrate;
    m_enccfg.framerate = cfg->framerate;
    
    m_enccfg.deviceType = cfg->deviceType;
    if (cfg->osVersion == AM_SC_OS_VERSION_WINDOWS)
    {
        m_enccfg.pixfmt = AV_PIX_FMT_VAAPI;
        hw_codec_name = (char*)hw_codec_names_win[cfg->codecIndex];
    }
    else if (cfg->osVersion == AM_SC_OS_VERSION_MAC) {
        m_enccfg.pixfmt = AV_PIX_FMT_VIDEOTOOLBOX;
        hw_codec_name = (char*)hw_codec_names_mac[cfg->codecIndex];
    }
    else {
        // TODO Set Error
        return;
    }

    strcpy_s(m_enccfg.hwDeviceTypeName, sizeof(m_enccfg.hwDeviceTypeName), cfg->hwDeviceTypeName);

    selCodec = avcodec_find_encoder_by_name((const char*)(hw_codec_name));
    if (selCodec) {
        m_enccfg.selCodec = selCodec;
        m_enccfg.codecId = hw_codec_ids[cfg->codecIndex];
        m_enccfg.encodingType = AM_SC_HW_ENCODING;
    }
    
    // fall back to software codecs
    if (!selCodec) {
        selCodec = avcodec_find_encoder_by_name(sw_codec_names[cfg->codecIndex]);
        if (selCodec) {
            m_enccfg.selCodec = selCodec;
            m_enccfg.codecId = sw_codec_ids[cfg->codecIndex];
            m_enccfg.encodingType = AM_SC_SW_ENCODING;
        }
    }
    
    // no suitable codecs found to encode
    if (!selCodec) {
        m_enccfg.encodingType = AM_SC_UNKNOWN_ENCODING;
        return;
    }
    if (m_enccfg.encodingType == AM_SC_HW_ENCODING) {
        InitHWEncoder(m_enccfg);
    } else {
        InitSWEncoder(m_enccfg);
    }
}

void ScreenCaptureEncoder::EncodeFramesOnHW(void *pixelBuffer, bool isTransmissionStarted) {
    std::lock_guard<std::mutex> lock(m_enc_mutex);
    if (!m_isEncConfigured) {
        return;
    }
    int ret;
    
    m_frame->data[3] = (uint8_t*)(uintptr_t)pixelBuffer;
    m_frame->pts = m_frameIndex++;

    if (isTransmissionStarted) {
        ret = avcodec_send_frame(m_codecCtx, m_frame);
        while (ret >= 0) {
            ret = avcodec_receive_packet(m_codecCtx, m_packet);
            if (ret == AVERROR(EAGAIN)) {
                break;
            } else if (ret == AVERROR_EOF) {
                break;
            }  else if (ret < 0)
            {
                lgr_err(lgra_video, "Error in receiving packet from Ecoder Ret: %d", ret);
                return;
            }
            std::cout << "Packet Size: " << m_packet->size << std::endl;
            av_packet_rescale_ts(m_packet,m_codecCtx->time_base, m_stream->time_base);
            m_packet->stream_index = m_stream->index;

            SendEncodedPacketToPacketizer(m_packet, m_frame, true);
            
            ret = av_interleaved_write_frame(m_formatCtx, m_packet);        //  FILE WRITE ONLY

            if (m_packet) {
                av_packet_unref(m_packet);
            }
        }
    } else {
        SendEncodedPacketToPacketizer(m_packet, m_frame, false);
    }
}
        
void ScreenCaptureEncoder::EncodeFramesOnSW(uint8_t *pixelBuffer, bool isTransmissionStarted) {
}

void ScreenCaptureEncoder::FlushEncoder() {
    int ret;
    std::lock_guard<std::mutex> lock(m_enc_mutex);
    if (!m_isEncConfigured) {
        return;
    }

    ret = avcodec_send_frame(m_codecCtx, NULL);

    while (ret == 0) {
        ret = avcodec_receive_packet(m_codecCtx, m_packet);
        if (ret == AVERROR(EAGAIN)) {
            break;
        } else if (ret == AVERROR_EOF) {
            break;
        }  else if (ret < 0)
        {
            return;
        }
        av_packet_rescale_ts(m_packet, m_codecCtx->time_base, m_stream->time_base);
        m_packet->stream_index = m_stream->index;
        ret = av_interleaved_write_frame(m_formatCtx, m_packet);        //  FILE WRITE ONLY
        av_packet_unref(m_packet);
        if (ret < 0) {
             break;
        }
    }
    m_isEncConfigured = false;
}

void ScreenCaptureEncoder::CloseEncoder() {
    std::lock_guard<std::mutex> lock(m_enc_mutex);

    //  FILE WRITE ONLY
    if (1) {
        if (m_formatCtx)
        {
            av_write_trailer(m_formatCtx);
            avformat_free_context(m_formatCtx);
            if (m_formatCtx->pb) {
                avio_close(m_formatCtx->pb);
            }
        }
    }
    if (m_video_frame.use_count() > 0) {
        m_video_frame.reset();
    }
    if (m_enc_packet.use_count() > 0) {
        m_enc_packet.reset();
    }
    if (m_hw_device_ctx) {
        av_buffer_unref(&m_hw_device_ctx);
        m_hw_device_ctx = nullptr;
    }
    if (m_frame) {
        av_frame_unref(m_frame);
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }
    if (m_hw_frames_ctx) {
        av_buffer_unref(&m_hw_frames_ctx);
    }
    if (m_packet) {
        av_packet_unref(m_packet);
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }
    m_isEncConfigured = false;
}

void ScreenCaptureEncoder::ReleaseEncResource() {
    if (m_video_display_player) {
        //m_video_display_player->close_display_resources();
        m_formatCtx = nullptr;
        m_stream = nullptr;
        m_codecCtx = nullptr;
    }
}

void ScreenCaptureEncoder::SendEncodedPacketToPacketizer(AVPacket *pkt, AVFrame *frame, bool has_data) {
    if (!m_video_display_player) {
        return;
    }
    av_packet_ref(m_enc_packet->packet, pkt);
    m_enc_packet->has_data = has_data;
    
    av_frame_ref(m_video_frame->frame, frame);
    m_video_frame->has_data = true;
    //m_video_display_player->send_screen_captured_packet(m_enc_packet, m_video_frame);
    av_packet_unref(m_enc_packet->packet);
    av_frame_unref(m_video_frame->frame);
}
