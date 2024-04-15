#include <chrono>
#include <iostream>
#include <string>

#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
//#include <opencv2/highgui/highgui_c.h>
#include <opencv2/imgproc.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/parseutils.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include "libswscale/swscale.h"
}

#include "rtc/rtc_producer.hpp"
#include "rtc/stream.hpp"

// tmp
#include "../../_imported/libdatachannel/examples/streamer/opusfileparser.hpp"
#include <fstream>

#ifdef _WIN32
#include <combaseapi.h>
#endif


class VideoStreamSource : public StreamSource {
private:
    cv::VideoCapture cap;
    cv::Mat cv_frame;
    cv::Mat yuv_frame;
    rtc::binary encoded_data;
    std::chrono::time_point<std::chrono::steady_clock> start_time;
    AVCodecContext* codec_context = nullptr;
    AVCodecParameters *codec_params = nullptr;

    AVFrame* av_frame = nullptr;
    AVPacket* packet = nullptr;

public:
    VideoStreamSource() = default;

    void start() override {
        cap.open(0); // open the default camera
        cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('h', '2', '6', '4'));

        if (!cap.isOpened()) {
            std::cout << "Waiting for the capture device to be opened." << std::endl;
            return;
        }
        cap >> cv_frame;
        if (cv_frame.empty()) {
            std::cerr << "Unexpected end of stream." << std::endl;
            return;
        }

        start_time = std::chrono::steady_clock::now();

        // find h.264 codec and allocate the codec context
        const auto* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        codec_context = avcodec_alloc_context3(codec);
        if (codec_context == nullptr) {
            throw std::runtime_error("Could not allocate codec context");
        }

        codec_context->bit_rate = 400000;
        codec_context->width = cap.get(cv::CAP_PROP_FRAME_WIDTH);
        codec_context->height = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
        codec_context->time_base = AVRational{1, 25};
        codec_context->framerate = AVRational{25, 1};
        codec_context->gop_size = 10;
        codec_context->max_b_frames = 1;
        codec_context->pix_fmt = AV_PIX_FMT_YUV420P;
        av_opt_set(codec_context->priv_data, "startcode", "1", 0);

        #ifdef _WIN32
        // essential to set hw_encoding=0 on Windows due to NVidia MF failure:
        // [h264_mf @ 0000024BC438BD40] MFT name: 'NVIDIA H.264 Encoder MFT'
        // [h264_mf @ 0000024BC438BD40] "format negotiation failed"
        av_opt_set(codec_context->priv_data, "hw_encoding", "1", 0); // TODO investigate
        #endif

        std::cout << "Width: " << codec_context->width << ", height: " << codec_context->height << std::endl;
        std::cout << "Input pixel format: " << av_get_pix_fmt_name(codec_context->pix_fmt) << std::endl;
        std::cout << "Output pixel format: " << av_get_pix_fmt_name(codec_context->codec->pix_fmts[0]) << std::endl;

        codec_params = avcodec_parameters_alloc();

        #ifdef _WIN32
        CoUninitialize();
        #endif
        auto ret = avcodec_open2(codec_context, codec, nullptr);
        if (ret < 0) {
            throw std::runtime_error("Could not open codec");
        }

        // initialize the frame
        // Allocate a frame to hold the converted image
        av_frame = av_frame_alloc();
        if (av_frame == nullptr) {
            std::cerr << "Failed to allocate converted frame" << std::endl;
            return;
        }

        int num_bytes = av_image_get_buffer_size(codec_context->pix_fmt, codec_context->width, codec_context->height, 1);
        std::vector<uint8_t> converted_frame_buffer(num_bytes);
        av_image_fill_arrays(av_frame->data, av_frame->linesize, converted_frame_buffer.data(), codec_context->pix_fmt, codec_context->width, codec_context->height, 1);
        av_frame->width = codec_context->width;
        av_frame->height = codec_context->height;
        av_frame->format = codec_context->pix_fmt;

        packet = av_packet_alloc();
    }

    void loadNextSample() override {
        cap >> cv_frame;
        if (cv_frame.empty()) {
            std::cerr << "Unexpected end of stream." << std::endl;
            return;
        }
        cv::cvtColor(cv_frame, yuv_frame, cv::COLOR_BGR2YUV_I420);

        av_frame->data[0] = yuv_frame.data;
        av_frame->data[1] = yuv_frame.data + codec_context->width * codec_context->height;
        av_frame->data[2] = yuv_frame.data + codec_context->width * codec_context->height * 5 / 4;
        av_frame->pts = av_gettime_relative() / 1000;

        int ret = avcodec_send_frame(codec_context, av_frame);
        while (ret >= 0) {
            ret = avcodec_receive_packet(codec_context, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                std::cerr << "Error encoding frame." << std::endl;
                return;
            }

//            if (packet->flags & AV_PKT_FLAG_KEY) {
//                uint8_t* nalus = nullptr;
//                int nalus_size = 0;
//
//                // Extract SPS and PPS data from codec context
//                for (int i = 0; i < codec_context->extradata_size - 3; i++) {
//                    if (codec_context->extradata[i] == 0x00 &&
//                        codec_context->extradata[i+1] == 0x00 &&
//                        codec_context->extradata[i+2] == 0x00 &&
//                        codec_context->extradata[i+3] == 0x01) {
//                        nalus = codec_context->extradata + i;
//                        nalus_size = i == 0 ? codec_context->extradata_size : i;
//                        break;
//                    }
//                }
//
//                // If SPS and PPS data found, insert it into encoded data buffer before the keyframe
//                if (nalus && nalus_size > 0) {
//                    encoded_data.insert(
//                            encoded_data.end(),
//                            reinterpret_cast<std::byte*>(nalus),
//                            reinterpret_cast<std::byte*>(nalus + nalus_size));
//                }
//            }

            encoded_data.insert(
                    encoded_data.end(),
                    reinterpret_cast<std::byte*>(packet->data),
                    reinterpret_cast<std::byte*>(packet->data + packet->size));
        }

        cv::pollKey();
    }

    auto getSample() -> rtc::binary override {
        auto sample = encoded_data;
        encoded_data.clear();

        if (0) {
            std::string fn = "/Users/denis.miller/dev/github/hela-surveillance/samples/n264/sampleou.h264";
            std::ofstream fw(fn, std::ios::out | std::ios::binary | std::ios_base::app);
            fw.write(reinterpret_cast<const char*>(sample.data()), sample.size());
        }

        LOGI << "Sending sample of size " << sample.size() << "b ";
        return sample;
    }

    void stop() override {
        av_frame_free(&av_frame);
        avcodec_parameters_free(&codec_params);
        avcodec_free_context(&codec_context);
        cap.release(); // release the camera
    }

    auto getSampleTime_us() -> uint64_t override {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
    }

    auto getSampleDuration_us() -> uint64_t override {
        return 1000000000 * codec_context->framerate.num / codec_context->framerate.den;
    }

};

auto createStream() -> std::shared_ptr<Stream> {
    auto video = std::make_shared<VideoStreamSource>();
//    auto audio = nullptr;
    auto audio = std::make_shared<OPUSFileParser>("/Users/denis.miller/dev/github/hela-surveillance/samples/opus", true);

    auto stream = std::make_shared<Stream>(video, audio);

    return stream;
}

auto main(int argc, char **argv) -> int {
    if (false) {
        std::string fn = "/home/miller/dev/github/hela-surveillance/samples/n264/sample.h264";
        std::ofstream fw(fn, std::ios::out | std::ios::binary);
    }

    auto stream = createStream();

    auto rtc = RtcProducer();
    rtc.setStream(stream);
    rtc.startServer();

    while (true) {
        std::string id;
        std::cout << "Enter to exit" << std::endl;
        std::cin >> id;
        std::cin.ignore();
        std::cout << "exiting" << std::endl;
        break;
    }

    return 0;
}
