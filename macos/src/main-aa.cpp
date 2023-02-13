#include <iostream>
#include <vector>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/highgui/highgui_c.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include "libswscale/swscale.h"
}

int main() {

    // Open the default camera using OpenCV
    cv::VideoCapture capture(0);
    if (!capture.isOpened()) {
        std::cerr << "Failed to open the camera" << std::endl;
        return 1;
    }

    // Allocate a frame to hold the captured image
    cv::Mat frame;
    capture >> frame;

    // Set up the encoder
    AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "Failed to find H.264 codec" << std::endl;
        return 1;
    }
    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        std::cerr << "Failed to allocate codec context" << std::endl;
        return 1;
    }
    codec_context->bit_rate = 400000;
    codec_context->width = frame.cols;
    codec_context->height = frame.rows;
    codec_context->time_base = {1, 30};
    codec_context->gop_size = 10;
    codec_context->max_b_frames = 1;
    codec_context->pix_fmt = AV_PIX_FMT_YUV420P;
    if (avcodec_open2(codec_context, codec, nullptr) < 0) {
        std::cerr << "Failed to open codec" << std::endl;
        return 1;
    }

//    // Allocate a frame to hold the encoded data
//    AVFrame* encoded_frame = av_frame_alloc();
//    if (!encoded_frame) {
//        std::cerr << "Failed to allocate encoded frame" << std::endl;
//        return 1;
//    }

    // Set up the packet to hold the encoded data
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        std::cerr << "Failed to allocate packet" << std::endl;
        return 1;
    }
    av_init_packet(packet);

    // Allocate a frame to hold the converted image
    AVFrame* converted_frame = av_frame_alloc();
    if (!converted_frame) {
        std::cerr << "Failed to allocate converted frame" << std::endl;
        return 1;
    }
    int num_bytes = av_image_get_buffer_size(codec_context->pix_fmt, codec_context->width, codec_context->height, 1);
    std::vector<uint8_t> converted_frame_buffer(num_bytes);
    av_image_fill_arrays(converted_frame->data, converted_frame->linesize, converted_frame_buffer.data(), codec_context->pix_fmt, codec_context->width, codec_context->height, 1);
    converted_frame->width = codec_context->width;
    converted_frame->height = codec_context->height;
    converted_frame->format = codec_context->pix_fmt;

    // Set up the image converter
    struct SwsContext* sws_context = sws_getContext(frame.cols, frame.rows, AV_PIX_FMT_BGR24, codec_context->width, codec_context->height, codec_context->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);

    // Encode and append the frames to the buffer
    std::vector<uint8_t> encoded_data;
    while (capture.read(frame)) {
        int cvLinesizes[1];
        cvLinesizes[0] = frame.step1();

        sws_scale(sws_context, &frame.data, cvLinesizes, 0, frame.rows, converted_frame->data, converted_frame->linesize);
        avcodec_send_frame(codec_context, converted_frame);
        while (avcodec_receive_packet(codec_context, packet) == 0) {
            std::copy(packet->data, packet->data + packet->size, std::back_inserter(encoded_data));
            av_packet_unref(packet);
        }
    }

    // Flush the encoder
    avcodec_send_frame(codec_context, nullptr);
    while (avcodec_receive_packet(codec_context, packet) == 0) {
        std::copy(packet->data, packet->data + packet->size, std::back_inserter(encoded_data));
        av_packet_unref(packet);
    }

    // Clean up resources
    sws_freeContext(sws_context);
    av_frame_free(&converted_frame);
    av_packet_free(&packet);
//    av_frame_free(&encoded_frame);
    avcodec_free_context(&codec_context);

    std::cout << "Encoded data size: " << encoded_data.size() << std::endl;

    return 0;
}
