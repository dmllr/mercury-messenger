#include <media/NdkMediaCodec.h>

#include "BasicUsageEnvironment.hh"


class CameraMediaSubsession : public OnDemandServerMediaSubsession {
public:
  enum Command {
    START,
    STOP
  };

  using ControlCallback = std::function<void(unsigned clientSessionId, void* token, Command command)>;

private:
  UsageEnvironment& env;
  AMediaCodec* codec;
  ControlCallback control_callback = 0;

public:
  CameraMediaSubsession(UsageEnvironment& env, Boolean reuseFirstSource)
      : OnDemandServerMediaSubsession(env, reuseFirstSource), env(env) {
  }

  static CameraMediaSubsession*
  createNew(UsageEnvironment& env, AMediaCodec* codec, ControlCallback control_callback) {
    auto subsession = new CameraMediaSubsession(env, true);
    subsession->codec = codec;
    subsession->control_callback = control_callback;

    return std::move(subsession);
  }

  virtual ~CameraMediaSubsession() override {
    Medium::close(this);
  }

protected:
  FramedSource* createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate) override {
    estBitrate = 57600;
    return CameraFramedSource::createNew(env, codec);
  }

  RTPSink* createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource) override {
    return H264VideoRTPSink::createNew(env, rtpGroupsock, rtpPayloadTypeIfDynamic);
  }

  void startStream(
      unsigned clientSessionId,
      void* streamToken,
      TaskFunc* rtcpRRHandler,
      void* rtcpRRHandlerClientData,
      unsigned short& rtpSeqNum,
      unsigned& rtpTimestamp,
      ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
      void* serverRequestAlternativeByteHandlerClientData) override {
    if (likely(control_callback))
      control_callback(clientSessionId, streamToken, START);

    OnDemandServerMediaSubsession::startStream(
        clientSessionId, streamToken, rtcpRRHandler, rtcpRRHandlerClientData, rtpSeqNum, rtpTimestamp,
        serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);
  }

  void deleteStream(unsigned clientSessionId, void*& streamToken) override {
    OnDemandServerMediaSubsession::deleteStream(clientSessionId, streamToken);

    if (likely(control_callback))
      control_callback(clientSessionId, streamToken, STOP);
  }
};
