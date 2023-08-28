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

#include "rtc/rtc_consumer.hpp"

// tmp
#include "../../_imported/libdatachannel/examples/streamer/opusfileparser.hpp"
#include <fstream>


int main(int argc, char **argv) {
    auto rtc = new RtcConsumer();
    rtc->setReceiver([](rtc::binary message) {
        LOGW << "Yes. " << message.size() << std::endl;
    });
    rtc->startClient();

    while (!rtc->hasPeer()) {
        LOGI << "Requesting stream... " << std::endl;
        rtc->requestStream();
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

    delete rtc;

    return 0;
}
