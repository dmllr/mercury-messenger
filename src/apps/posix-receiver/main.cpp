#include <chrono>
#include <iostream>
#include <string>

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
}

#include "rtc/rtc_consumer.hpp"

// tmp
#include <fstream>


int main(int argc, char** argv) {
  // play the stream

  // Create AVFormatContext and AVCodecContext (use your H.264 decoder)
  AVFormatContext* formatContext = avformat_alloc_context();
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  AVCodecContext* codecContext = avcodec_alloc_context3(codec);

  // Fill in codec parameters based on your stream properties
  codecContext->width = 1280;  // Set your width
  codecContext->height = 720;  // Set your height

  // Open codec
  avcodec_open2(codecContext, codec, nullptr);

  // Allocate frame for decoded data
  AVFrame* frame = av_frame_alloc();

  AVPacket packet;
  //    av_init_packet(&packet);
  av_new_packet(&packet, 10000);

  // get the stream
  auto rtc = RtcConsumer();
  rtc.setReceiver([&codecContext, &frame, &packet](rtc::binary message) {
    static auto sep = std::array<std::byte, 4>{std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}};
    LOGW << "Yes. " << message.size() << std::endl;
    //        message.insert(
    //                message.begin(),
    //                sep.data(),
    //                sep.data() + sep.size()
    //        );
    packet.data = reinterpret_cast<uint8_t*>(message.data());
    packet.size = message.size();

    if (0) {
      std::string fn = "/Users/denis.miller/dev/github/hela-surveillance/samples/n264/samplein.h264";
      std::ofstream fw(fn, std::ios::out | std::ios::binary | std::ios_base::app);
      fw.write(reinterpret_cast<const char*>(message.data()), message.size());
    }

    // Decode the packet
    avcodec_send_packet(codecContext, &packet);
    avcodec_receive_frame(codecContext, frame);

    // Convert AVFrame to OpenCV Mat
    cv::Mat cvFrame(codecContext->height, codecContext->width, CV_8UC3, frame->data[0], frame->linesize[0]);

    // Display the frame
    cv::imshow("Video Frame", cvFrame);
  });
  rtc.startClient();

  while (!rtc.hasPeer()) {
    LOGI << "Requesting stream... " << std::endl;
    rtc.requestStream();
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
  }

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
