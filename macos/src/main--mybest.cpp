#include <chrono>
#include <iostream>
#include <string>

#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/highgui/highgui_c.h>
#include <opencv2/imgproc.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include "libswscale/swscale.h"
}

#include "rtc/rtc_manager.hpp"
#include "rtc/stream.hpp"

// tmp
#include "../../_imported/libdatachannel/examples/streamer/opusfileparser.hpp"


class VideoStreamSource : public StreamSource {
private:
    cv::VideoCapture cap;
    cv::Mat frame;
    rtc::binary encoded_data;
    std::chrono::time_point<std::chrono::steady_clock> start_time;
    AVCodecContext *codec_context;
    AVFrame *converted_frame;

public:
    VideoStreamSource() : cap(0) {} // default constructor, initialize cap with default camera

    void start() override {
        cap.open(0); // open the default camera
        start_time = std::chrono::steady_clock::now();

        // find h.264 codec and allocate the codec context
        AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        codec_context = avcodec_alloc_context3(codec);
        if (!codec_context) {
            throw std::runtime_error("Could not allocate codec context");
        }

        codec_context->bit_rate = 400000;
        codec_context->width = cap.get(cv::CAP_PROP_FRAME_WIDTH);
        codec_context->height = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
        codec_context->time_base = (AVRational){1, 25};
        codec_context->framerate = (AVRational){25, 1};
        codec_context->gop_size = 10;
        codec_context->max_b_frames = 1;
        codec_context->pix_fmt = AV_PIX_FMT_YUV420P;

        if (avcodec_open2(codec_context, codec, nullptr) < 0) {
            throw std::runtime_error("Could not open codec");
        }

        // initialize the frame
        // Allocate a frame to hold the converted image
        AVFrame* converted_frame = av_frame_alloc();
        if (!converted_frame) {
            std::cerr << "Failed to allocate converted frame" << std::endl;
            return;
        }

        int num_bytes = av_image_get_buffer_size(codec_context->pix_fmt, codec_context->width, codec_context->height, 1);
        std::vector<uint8_t> converted_frame_buffer(num_bytes);
        av_image_fill_arrays(converted_frame->data, converted_frame->linesize, converted_frame_buffer.data(), codec_context->pix_fmt, codec_context->width, codec_context->height, 1);
        converted_frame->width = codec_context->width;
        converted_frame->height = codec_context->height;
        converted_frame->format = codec_context->pix_fmt;

    }

    AVFrame *cvMatToAVFrame(cv::Mat *image, AVFrame *frame) {
        int width = image->cols;
        int height = image->rows;
        int cvLinesizes[1];
        cvLinesizes[0] = image->step1();
        if (frame == nullptr) {
            frame = av_frame_alloc();
            av_image_alloc(frame->data, frame->linesize, width, height,
                           AVPixelFormat::AV_PIX_FMT_YUV420P, 1);
        }
        auto conversion = sws_getContext(
                width, height, AVPixelFormat::AV_PIX_FMT_BGR24, width, height,
                codec_context->pix_fmt, SWS_FAST_BILINEAR, NULL, NULL, NULL);
        sws_scale(conversion, &image->data, cvLinesizes, 0, height, frame->data,
                  frame->linesize);
        sws_freeContext(conversion);
        return frame;
    }

    void loadNextSample() override {
        cap >> frame;
        imshow("Video Capture", frame);

////        auto sws_context = sws_getContext(
////                frame.cols, frame.rows, AVPixelFormat::AV_PIX_FMT_BGR24, frame.cols, frame.rows,
////                converted_frame->format, SWS_FAST_BILINEAR, NULL, NULL, NULL);
//        auto sws_context = sws_getContext(
//                frame.cols, frame.rows, AV_PIX_FMT_BGR24, codec_context->width, codec_context->height,
//                codec_context->pix_fmt, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
//
//        int cvLinesizes[1];
//        cvLinesizes[0] = frame.step1();
//
/*
        AVPacket* packet = av_packet_alloc();
        if (!packet) {
            std::cerr << "Failed to allocate packet" << std::endl;
            return;
        }
//
//        sws_scale(sws_context, &frame.data, cvLinesizes, 0, frame.rows, converted_frame->data, converted_frame->linesize);

        cvMatToAVFrame(&frame, converted_frame);

        avcodec_send_frame(codec_context, converted_frame);
        while (avcodec_receive_packet(codec_context, packet) == 0) {
//            std::copy(packet->data, packet->data + packet->size, std::back_inserter(encoded_data));
            encoded_data.insert(
                    encoded_data.end(),
                    reinterpret_cast<std::byte*>(&packet->data),
                    reinterpret_cast<std::byte*>(&packet->data[packet->size]));

            av_packet_unref(packet);
        }

//        sws_freeContext(sws_context);
        av_frame_free(&converted_frame);
        av_packet_free(&packet);
*/

        // convert the frame to AVFrame format
        // assume mat.channels() == 3
        av_image_fill_arrays(converted_frame->data, converted_frame->linesize, frame.data, AV_PIX_FMT_BGR24, frame.cols, frame.rows, 1);

        // encode the frame to h.264 format
        AVPacket *packet = av_packet_alloc();
        int ret = 0;
        ret = avcodec_send_frame(codec_context, converted_frame);
        if (ret < 0) {
            LOGE << "EEEEEEEEEEEEEEEE " << ret << std::endl;
        }

        ret = avcodec_receive_packet(codec_context, packet);
        if (ret == 0) {
            LOGI << "############### " << packet->size << std::endl;
            // append the result to encoded_data
            encoded_data.insert(
                    encoded_data.end(),
                    reinterpret_cast<std::byte*>(&packet->data),
                    reinterpret_cast<std::byte*>(&packet->data + packet->size));
        }
        av_packet_free(&packet);
    }

    rtc::binary getSample() override {
        auto sample = encoded_data;
        encoded_data.clear();
        return sample;
    }

    void stop() override {
        cap.release(); // release the camera
        avcodec_free_context(&codec_context);
    }

    uint64_t getSampleTime_us() override {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
    }

    uint64_t getSampleDuration_us() override {
//        return encoded_data.size() * 8 * 1000000000 / codec_context->bit_rate;
        return 1000000000 * codec_context->framerate.num / codec_context->framerate.den;
    }

};

std::shared_ptr<Stream> createStream() {
    auto video = std::make_shared<VideoStreamSource>();
    auto audio = std::make_shared<OPUSFileParser>("/Users/denis.miller/dev/github/hela-surveillance/samples/opus", true);

    auto stream = std::make_shared<Stream>(video, audio);

    return stream;
}

int main(int argc, char **argv) {
    auto stream = createStream();

    cv::namedWindow("Video Capture", CV_WINDOW_AUTOSIZE);

    auto rtc_manager = new RtcManager();
    rtc_manager->setStream(stream);
    rtc_manager->start();

    while (true) {
        std::string id;
        std::cout << "Enter to exit" << std::endl;
        std::cin >> id;
        std::cin.ignore();
        std::cout << "exiting" << std::endl;
        break;
    }

    delete rtc_manager;

    return 0;
}
