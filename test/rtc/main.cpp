#include <iostream>

#include "../../android/hela-surveillance/app-surveillance-camera/src/main/cpp/src/rtc/rtc_manager.hpp"
#include "../../android/hela-surveillance/app-surveillance-camera/src/main/cpp/src/rtc/h264fileparser.hpp"
#include "../../android/hela-surveillance/app-surveillance-camera/src/main/cpp/src/rtc/opusfileparser.hpp"


std::shared_ptr<Stream> createStream() {
	// video source
	auto video = std::make_shared<H264FileParser>("/Users/denis.miller/dev/github/hela-surveillance/samples/h264", 30, true);
	// audio source
	auto audio = std::make_shared<OPUSFileParser>("/Users/denis.miller/dev/github/hela-surveillance/samples/opus", true);

	auto stream = std::make_shared<Stream>(video, audio);

	return stream;
}


int main() {
//	mainmain(0, nullptr);

	auto stream = createStream();

	auto rtc_manager = new RtcManager();
	rtc_manager->setStream(stream);
	rtc_manager->start();
	LOGI << "Test RTC";

	while (true) {
		std::string id;
		std::cout << "Enter to exit" << std::endl;
		std::cin >> id;
		std::cin.ignore();
		std::cout << "exiting" << std::endl;
		break;
	}

	delete rtc_manager;
}
