#pragma once

#include <string>
#include <vector>
#include <fstream>

#include "rtc/stream.hpp"


class CameraBufferSource: public StreamSource {
	static constexpr auto target = CaptureTarget::RTC_ENCODER_SURFACE;

	AMediaCodecBufferInfo info;
	uint8_t* buffer = nullptr;
	uint64_t timeStart_us = 0;
    rtc::binary encoded_data;

protected:
	std::shared_ptr<CameraManager> camera_manager;

public:
	CameraBufferSource(std::shared_ptr<CameraManager> camera_manager) : camera_manager(camera_manager) {
	}

    virtual void start() override {
		camera_manager->codec()->start();
		camera_manager->addConsumer(target);
//		loadNextSample();
	}

    virtual void stop() override {
		camera_manager->removeConsumer(target);
		camera_manager->codec()->stop();
    }

    virtual void loadNextSample() override {
		const auto& codec = camera_manager->codec()->getCodec();
		auto index = AMediaCodec_dequeueOutputBuffer(codec, &info, 100000);  // TODO const timeout
		if (likely(index >= 0)) {
			if (unlikely(info.flags == AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)) {
				const auto config_info = info;
                const auto config_index = index;
                const auto config_buffer = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(config_index), NULL);
				index = AMediaCodec_dequeueOutputBuffer(codec, &info, 100000);  // TODO const timeout
				buffer = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(index), NULL);

				timeStart_us = info.presentationTimeUs;

				encoded_data.clear();
                encoded_data.insert(encoded_data.end(),
							  reinterpret_cast<std::byte*>(&config_buffer[config_info.offset]),
							  reinterpret_cast<std::byte*>(&config_buffer[config_info.offset + config_info.size]));
                encoded_data.insert(encoded_data.end(),
							  reinterpret_cast<std::byte*>(&buffer[info.offset]),
							  reinterpret_cast<std::byte*>(&buffer[info.size]));

				AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(config_index), false);
				AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(index), false);
			} else {
				buffer = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(index), NULL);
                encoded_data.clear();
                encoded_data.insert(encoded_data.end(),
							  reinterpret_cast<std::byte*>(&buffer[info.offset]),
							  reinterpret_cast<std::byte*>(&buffer[info.size]));
				AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(index), false);
			}
		} else if (index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
			LOGI << "Output buffers changed";
            encoded_data.clear();
		} else if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
			auto format = AMediaCodec_getOutputFormat(codec);
			LOGI << "Format set to: " << AMediaFormat_toString(format);
			AMediaFormat_delete(format);
            encoded_data.clear();
		} else if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
			LOGW << "No output buffer right now";
            encoded_data.clear();
		} else {
			LOGI << "AMediaCodec_dequeueOutputBuffer unexpected return: " << index;
            encoded_data.clear();
		}
    }

    rtc::binary getSample() override {
        auto sample = encoded_data;
        encoded_data.clear();

        LOGI << "Sending sample of size " << sample.size() << "b " << std::endl;

        return sample;
    }

    uint64_t getSampleTime_us() override {
        return info.presentationTimeUs - timeStart_us;
    }

    uint64_t getSampleDuration_us() override {
        return 1000000000 / 30; // todo;
    }

//	void deliver() {
//		auto v = std::vector<uint8_t>(&buffer[0] + info.offset, &buffer[0] + info.offset + info.size);
//		sample = *reinterpret_cast<std::vector<std::byte> *>(&v);
//		sampleTime_us = info.presentationTimeUs - timeStart_us;
////		sampleTime_us += sampleDuration_us;
//
////		auto z = (std::byte)0;
////		auto o = (std::byte)1;
////		unsigned long long i = 0;
////		while (i < sample.size()) {
////			while (i + 3 < sample.size() && (sample[i] != z || sample[i+1] != z || sample[i+2] != z || sample[i+3] != o))
////				i++;
////			if (i + 4 >= sample.size())
////				break;
////			auto naluStartIndex = i;
////			i += 4;
////			while (i + 4 < sample.size() && (sample[i+1] != z || sample[i+2] != z || sample[i+3] != z || sample[i+4] != o))
////				i++;
////			if (i >= sample.size())
////				break;
////			auto naluEndIndex = i;
////
////			LOGD << "NALU from " << naluStartIndex << " to " << naluEndIndex << " out of " << sample.size();
////
////			auto header = reinterpret_cast<rtc::NalUnitHeader*>(sample.data() + naluStartIndex + 4);
////			auto type = header->unitType();
////			switch (type) {
////				case 7:
////					previousUnitType7 = {sample.begin() + naluStartIndex, sample.begin() + naluEndIndex};
////					break;
////				case 8:
////					previousUnitType8 = {sample.begin() + naluStartIndex, sample.begin() + naluEndIndex};;
////					break;
////				case 5:
////					previousUnitType5 = {sample.begin() + naluStartIndex, sample.begin() + naluEndIndex};;
////					break;
////			}
////		}
//
//
////		auto f = std::ofstream("hello.mp4", std::ios::out | std::ios::app | std::ios::binary);
////		f.write((const char*)&buffer[0], size);
////		f.flush();
////		f.close();
//
////		auto lengthPtr = (uint8_t*) (sample.data() + 5);
////		*lengthPtr = *lengthPtr & 0b00000001;
//
//		LOGI << std::hex
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[0]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[1]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[2]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[3]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[4]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[5]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[6]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[7]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[8]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[9]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[10]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[11]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[12]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[13]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[14]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[15]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[16]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[17]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[18]) << " "
//				 << std::setfill('0') << std::setw(2) << static_cast<unsigned>(buffer[19]) << " "
//				 ;
//	}

//	std::vector<std::byte> initialNALUS() {
//		std::vector<std::byte> units{};
//		if (previousUnitType7.has_value()) {
//			auto nalu = previousUnitType7.value();
//			units.insert(units.end(), nalu.begin(), nalu.end());
//		}
//		if (previousUnitType8.has_value()) {
//			auto nalu = previousUnitType8.value();
//			units.insert(units.end(), nalu.begin(), nalu.end());
//		}
//		if (previousUnitType5.has_value()) {
//			auto nalu = previousUnitType5.value();
//			units.insert(units.end(), nalu.begin(), nalu.end());
//		}
//		return units;
//	}

//private:
//	std::optional<std::vector<std::byte>> previousUnitType5 = std::nullopt;
//	std::optional<std::vector<std::byte>> previousUnitType7 = std::nullopt;
//	std::optional<std::vector<std::byte>> previousUnitType8 = std::nullopt;

};
