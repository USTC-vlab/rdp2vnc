#pragma once
#include <thread>
#include <mutex>
#include <memory>
#include <atomic>
#include <rfb/CConnection.h>
#include <rfb/PixelFormat.h>
#include <rfb/Rect.h>
#include <network/TcpSocket.h>

class RDPSession;
class VNCClient : public rfb::CConnection {
public:
  VNCClient(RDPSession* rdp_);
  ~VNCClient();
  bool init(const char* host, uint16_t port);
  virtual void initDone();
  virtual void setCursor(int width, int height, const rfb::Point& hotspot, const rdr::U8* data);
  virtual void setCursorPos(const rfb::Point& pos);
  virtual void setColourMapEntries(int firstColour, int nColours, rdr::U16* rgbs);
  virtual void bell();
  virtual void framebufferUpdateStart();
  virtual void framebufferUpdateEnd();
  virtual bool dataRect(const rfb::Rect& r, int encoding);
  bool waitInit();
  void startThread();
  void stop();
  int width();
  int height();
  int getFd();
  void process();
private:
  void eventLoop();
  std::unique_ptr<std::thread> thread_;
  std::unique_ptr<network::Socket> socket;
  RDPSession* rdp;
  rfb::ManagedPixelBuffer* pb;
  bool hasInitialized;
  std::atomic_bool needStop;
  std::atomic_bool hasStopped;
  bool hasStarted;
};