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
//#include "libswscale/swscale.h"
}

#include "rtc/rtc_manager.hpp"
#include "rtc/stream.hpp"

// tmp
#include "../../_imported/libdatachannel/examples/streamer/opusfileparser.hpp"


class VideoStreamSource : public StreamSource {
public:
    VideoStreamSource() {
        auto codec_name = "libx264";

        int ret;

        codec = avcodec_find_encoder_by_name(codec_name);
        if (!codec) {
            fprintf(stderr, "Codec '%s' not found\n", codec_name);
            exit(1);
        }

        c = avcodec_alloc_context3(codec);
        if (!c) {
            fprintf(stderr, "Could not allocate video codec context\n");
            exit(1);
        }

        pkt = av_packet_alloc();
        if (!pkt)
            exit(1);

        /* put sample parameters */
        c->bit_rate = 400000;
        /* resolution must be a multiple of two */
        c->width = 352;
        c->height = 288;
        /* frames per second */
        c->time_base = (AVRational) {1, 25};
        c->framerate = (AVRational) {25, 1};

        /* emit one intra frame every ten frames
         * check frame pict_type before passing frame
         * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
         * then gop_size is ignored and the output of encoder
         * will always be I frame irrespective to gop_size
         */
        c->gop_size = 10;
        c->max_b_frames = 1;
        c->pix_fmt = AV_PIX_FMT_YUV420P;

        if (codec->id == AV_CODEC_ID_H264)
            av_opt_set(c->priv_data, "preset", "slow", 0);

        /* open it */
        ret = avcodec_open2(c, codec, NULL);
        if (ret < 0) {
            fprintf(stderr, "Could not open codec");
            exit(1);
        }

//    f = fopen(filename, "wb");
//    if (!f) {
//        fprintf(stderr, "Could not open %s\n", filename);
//        exit(1);
//    }

        frame = av_frame_alloc();
        if (!frame) {
            fprintf(stderr, "Could not allocate video frame\n");
            exit(1);
        }
        frame->format = c->pix_fmt;
        frame->width = c->width;
        frame->height = c->height;

        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) {
            fprintf(stderr, "Could not allocate the video frame data\n");
            exit(1);
        }
    }

    ~VideoStreamSource() {
        avcodec_free_context(&c);
        av_frame_free(&frame);
        av_packet_free(&pkt);
    }

    void loadNextSample() override {
        cap >> cvf;

        // convert the OpenCV Mat to an AVFrame
        cv::cvtColor(cvf, yuvMat, cv::COLOR_BGR2YUV_I420);
        av_image_fill_arrays(frame->data, frame->linesize, yuvMat.data,
                             AV_PIX_FMT_YUV420P, frame->width, frame->height, 32);

        encode();

        imshow("Video Capture", cvf);
        cv::waitKey(1);
    }

    rtc::binary getSample() override {
        return sample;
    }

    void start() override {
        cvf = cv::Mat();
        cap = cv::VideoCapture();
        cap.open(0);
        if (!cap.isOpened()) {
            return;
        }

        timeStart_us = std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now()).time_since_epoch().count();

        // create a window and display the video capture
        cv::namedWindow("Video Capture", CV_WINDOW_AUTOSIZE);
    }

    void stop() override {
    }

    uint64_t getSampleTime_us() override {
//        return std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
        timeStart_us += getSampleDuration_us();
        return timeStart_us;
    }

    uint64_t getSampleDuration_us() override {
        return 1e+9 * (c->framerate.num / c->framerate.den);
    }

protected:
    void encode() {
        int ret;

        /* send the frame to the encoder */
        if (frame)
            printf("Send frame %3ld\n", frame->pts);

        ret = avcodec_send_frame(c, frame);
        if (ret < 0) {
            fprintf(stderr, "Error sending a frame for encoding\n");
            exit(1);
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(c, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                return;
            else if (ret < 0) {
                fprintf(stderr, "Error during encoding\n");
                exit(1);
            }

            printf("Write packet %3ld (size=%5d)\n", pkt->pts, pkt->size);

            // ############# stream the packet
//            fwrite(pkt->data, 1, pkt->size, outfile);
            sample.clear();
            sample.insert(
                    sample.end(),
                    reinterpret_cast<std::byte*>(&pkt->data),
                    reinterpret_cast<std::byte*>(&pkt->data[pkt->size]));

            av_packet_unref(pkt);
        }
    }

private:
    // capture
    cv::VideoCapture cap;
    cv::Mat cvf;
    cv::Mat yuvMat;

    // encoding
    const AVCodec *codec;
    AVCodecContext *c = nullptr;
    AVFrame *frame;
    AVPacket *pkt;

    uint64_t timeStart_us = 0;

    // endoded buffer
    rtc::binary sample;
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



/*
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/highgui/highgui_c.h>
#include <opencv2/videoio/legacy/constants_c.h>

int main(int argc, const char * argv[]) {
    auto frame = cv::Mat();
    auto cap = cv::VideoCapture();
    cap.set(CV_CAP_PROP_FOURCC, CV_FOURCC('H', '2', '6', '4'));

    // try to open camera
    cap.open(0);
    if (!cap.isOpened()) {
        return -1;
    }

    // create a window and display the video capture
    cv::namedWindow("Video Capture", CV_WINDOW_AUTOSIZE);
    for (int i=0; i<100; i++) {
        cap >> frame;
        imshow("Video Capture", frame);
        cv::waitKey(1);
    }

    return 0;
}
 */