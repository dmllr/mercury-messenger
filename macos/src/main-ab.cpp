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
public:
    int init_resources() {
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

        // Allocate a frame to hold the encoded data
        AVFrame* encoded_frame = av_frame_alloc();
        if (!encoded_frame) {
            std::cerr << "Failed to allocate encoded frame" << std::endl;
            return 1;
        }

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
    }

    void release_resources() {
        sws_freeContext(sws_context);
        av_frame_free(&converted_frame);
        av_packet_free(&packet);
        av_frame_free(&encoded_frame);
        avcodec_free_context(&codec_context);
    }


    VideoStreamSource() {
        init_resources();
    }

    ~VideoStreamSource() {
        release_resources();
    }

    void loadNextSample() override {
        capture.read(frame);

        int cvLinesizes[1];
        cvLinesizes[0] = frame.step1();

        sws_scale(sws_context, &frame.data, cvLinesizes, 0, frame.rows, converted_frame->data, converted_frame->linesize);
        avcodec_send_frame(codec_context, converted_frame);
        while (avcodec_receive_packet(codec_context, packet) == 0) {
            std::copy(packet->data, packet->data + packet->size, std::back_inserter(encoded_data));
            av_packet_unref(packet);
        }

        cap >> cvf;

        // convert the OpenCV Mat to an AVFrame
//        cvmatToAvframe();

        av_image_fill_arrays(frame->data, frame->linesize, cvf.data,
                             AV_PIX_FMT_YUV420P, frame->width, frame->height, 32);

//        cv::cvtColor(cvf, yuvMat, cv::COLOR_BGR2YUV_I420);
//        av_image_fill_arrays(frame->data, frame->linesize, yuvMat.data,
//                             AV_PIX_FMT_YUV420P, frame->width, frame->height, 32);

        imshow("Video Capture", frame);

        encode();
    }

    rtc::binary getSample() override {
        auto ret = sample;
        sample.clear();
        timeStart_us += getSampleDuration_us();
        return ret;
    }

    void start() override {
        timeStart_us = 0;//std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now()).time_since_epoch().count();

        // create a window and display the video capture
        cv::namedWindow("Video Capture", CV_WINDOW_AUTOSIZE);
    }

    void stop() override {
    }

    uint64_t getSampleTime_us() override {
//        return std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
        return timeStart_us;
    }

    uint64_t getSampleDuration_us() override {
        return 1000000000 * c->framerate.num / c->framerate.den;
    }

private:
    uint64_t timeStart_us = 0;
    rtc::binary encoded_data;
};

std::shared_ptr<Stream> createStream() {
    auto video = std::make_shared<VideoStreamSource>();
    auto audio = std::make_shared<OPUSFileParser>("/Users/denis.miller/dev/github/hela-surveillance/samples/opus", true);

    auto stream = std::make_shared<Stream>(video, audio);

    return stream;
}

int main(int argc, char **argv) {
    auto stream = createStream();

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
