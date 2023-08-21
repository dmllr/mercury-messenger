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

#include "rtc/rtc_manager.hpp"

// tmp
#include "../../_imported/libdatachannel/examples/streamer/opusfileparser.hpp"
#include <fstream>


int main(int argc, char **argv) {
    auto rtc_manager = new RtcManager();
    rtc_manager->setReceiver([](rtc::binary message) {
        LOGW << "Yes. " << message.size() << std::endl;
    });
    rtc_manager->startClient();

    while (true) {
        std::string id;
        std::cout << "Enter to exit" << std::endl;
        std::cin >> id;
        if (id == "r") {
            rtc_manager->reload();
        }
        std::cin.ignore();
        std::cout << "exiting" << std::endl;
        break;
    }

    delete rtc_manager;

    return 0;
}
