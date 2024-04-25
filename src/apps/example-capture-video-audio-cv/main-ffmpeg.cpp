#include <iostream>

#include "opencv2/highgui.hpp"
#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/avutil.h>
}

using namespace cv;
using namespace std;

#define SAMPLE_FORMAT AV_SAMPLE_FMT_S16
#define CHANNELS 2
#define SAMPLE_RATE 44100

int main() {
//  // Initialize FFmpeg
//  av_register_all();
//  avformat_network_init();

  avdevice_register_all();

  // Open default camera
  VideoCapture cap(0);
  if (!cap.isOpened()) {
    cerr << "Error: Couldn't open the camera" << endl;
    return -1;
  }

  // Open default camera with hardware acceleration disabled
  AVFormatContext *cameraFormatCtx = avformat_alloc_context();
  const AVInputFormat* cameraInputFmt = av_find_input_format("v4l2");
  AVDictionary *options = nullptr;
  av_dict_set(&options, "input_format", "mjpeg", 0); // Specify input format if needed
  av_dict_set(&options, "hwaccel", "none", 0); // Disable hardware acceleration
  if (avformat_open_input(&cameraFormatCtx, "video=HD Webcam C525", cameraInputFmt, &options) < 0) {
    cerr << "Error: Couldn't open camera stream" << endl;
    return -1;
  }
  av_dict_free(&options);
  if (avformat_find_stream_info(cameraFormatCtx, nullptr) < 0) {
    cerr << "Error: Couldn't find stream information" << endl;
    return -1;
  }

  // Find camera video stream
  int cameraVideoStreamIndex = -1;
  for (unsigned int i = 0; i < cameraFormatCtx->nb_streams; i++) {
    if (cameraFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      cameraVideoStreamIndex = i;
      break;
    }
  }
  if (cameraVideoStreamIndex == -1) {
    cerr << "Error: Couldn't find camera video stream" << endl;
    return -1;
  }

  // Find camera audio stream
  int cameraAudioStreamIndex = -1;
  for (unsigned int i = 0; i < cameraFormatCtx->nb_streams; i++) {
    if (cameraFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      cameraAudioStreamIndex = i;
      break;
    }
  }
  if (cameraAudioStreamIndex == -1) {
    cerr << "Error: Couldn't find camera audio stream" << endl;
    return -1;
  }

  // Initialize audio buffer
  AVFrame *audioFrame = av_frame_alloc();
  uint8_t **audioData = nullptr;
  int audioSamples = av_samples_alloc_array_and_samples(&audioData, nullptr, CHANNELS, 512, SAMPLE_FORMAT, 0);

  // Capture loop
  Mat frame;
  while (true) {
    // Capture frame from camera
    cap >> frame;
    if (frame.empty()) {
      cerr << "Error: Couldn't capture frame" << endl;
      break;
    }

    // Display the frame
    imshow("Camera", frame);

    // Capture audio from camera
    AVPacket packet;
    av_init_packet(&packet);
    if (av_read_frame(cameraFormatCtx, &packet) < 0) {
      cerr << "Error: Couldn't read audio frame" << endl;
      break;
    }
    if (packet.stream_index == cameraAudioStreamIndex) {
      AVCodecParameters *audioCodecParams = cameraFormatCtx->streams[cameraAudioStreamIndex]->codecpar;
      AVCodecContext *audioCodecCtx = avcodec_alloc_context3(nullptr);
      avcodec_parameters_to_context(audioCodecCtx, audioCodecParams);
      avcodec_send_packet(audioCodecCtx, &packet);
      while (avcodec_receive_frame(audioCodecCtx, audioFrame) == 0) {
        for (int i = 0; i < audioFrame->nb_samples; i++) {
          for (int ch = 0; ch < CHANNELS; ch++) {
            ((short *)audioData[ch])[i] = ((short *)audioFrame->data[ch])[i];
          }
        }
      }
      avcodec_free_context(&audioCodecCtx);
    }
    av_packet_unref(&packet);

    // If 'q' is pressed, exit loop
    if (waitKey(10) == 'q') {
      break;
    }
  }

  // Free resources
  av_frame_free(&audioFrame);
  avformat_close_input(&cameraFormatCtx);
  avformat_free_context(cameraFormatCtx);
  av_freep(&audioData[0]);
  av_freep(&audioData);

  // Release camera
  cap.release();
  destroyAllWindows();

  return 0;
}
