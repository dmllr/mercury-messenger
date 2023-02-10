#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/highgui/highgui_c.h>
#include <opencv2/videoio/legacy/constants_c.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include "libswscale/swscale.h"
}

class CameraStreamer {
public:
    CameraStreamer() {
        auto codec_name = "libx264";

        int ret;

        /* find the mpeg1video encoder */
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
            fprintf(stderr, "Could not open codec: %s\n", av_err2str(ret));
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

    ~CameraStreamer() {
        /* flush the encoder */
        encode(nullptr);

//        /* Add sequence end code to have a real MPEG file.
//           It makes only sense because this tiny examples writes packets
//           directly. This is called "elementary stream" and only works for some
//           codecs. To create a valid file, you usually need to write packets
//           into a proper file format or protocol; see muxing.c.
//         */
//        uint8_t endcode[] = {0, 0, 1, 0xb7};
//        if (codec->id == AV_CODEC_ID_MPEG1VIDEO || codec->id == AV_CODEC_ID_MPEG2VIDEO)
//            fwrite(endcode, 1, sizeof(endcode), f);
//        fclose(f);

        avcodec_free_context(&c);
        av_frame_free(&frame);
        av_packet_free(&pkt);
    }

    AVFrame *cvmatToAvframe(cv::Mat *image, AVFrame *frame) {
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
                (AVPixelFormat)frame->format, SWS_FAST_BILINEAR, NULL, NULL, NULL);
        sws_scale(conversion, &image->data, cvLinesizes, 0, height, frame->data,
                  frame->linesize);
        sws_freeContext(conversion);
        return frame;
    }

    int start() {
        auto cvf = cv::Mat();

        cv::VideoCapture cap = cv::VideoCapture();
        cap.open(0);
        if (!cap.isOpened()) {
            return -1;
        }

        // create a window and display the video capture
        cv::namedWindow("Video Capture", CV_WINDOW_AUTOSIZE);
        for (int i=0; i<100; i++) {
            cap >> cvf;

            cvmatToAvframe(&cvf, frame);
            encode(frame);

            imshow("Video Capture", cvf);
            cv::waitKey(1);
        }

        return 0;
    }

protected:
    void encode(AVFrame *frame) {
        int ret;

        /* send the frame to the encoder */
        if (frame)
            printf("Send frame %3\n", frame->pts);

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

            printf("Write packet %3 (size=%5d)\n", pkt->pts, pkt->size);

            // ############# stream the packet
//            fwrite(pkt->data, 1, pkt->size, outfile);

            av_packet_unref(pkt);
        }
    }

private:
    // encoding
    const AVCodec *codec;
    AVCodecContext *c = nullptr;
    AVFrame *frame;
    AVPacket *pkt;
};

int main(int argc, char **argv) {
    auto cs = CameraStreamer();
    cs.start();

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