#include <iostream>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <rfb/CConnection.h>
#include <rfb/Timer.h>
#include <rfb/SecurityClient.h>
#include <rfb/Security.h>
#include <rfb/PixelBuffer.h>
#include <rfb/PixelFormat.h>
#include <rfb/UserPasswdGetter.h>
#include <rfb/UserMsgBox.h>
#ifdef HAVE_GNUTLS
#include <rfb/CSecurityTLS.h>
#endif
#include <network/TcpSocket.h>
#include "vnc2rdp/VNCClient.h"
#include "vnc2rdp/RDPSession.h"

using namespace std;
using namespace rfb;

class UPG : public UserPasswdGetter {
public:
  virtual void getUserPasswd(bool scure, char** user, char** password) {}
};

class UMB : public UserMsgBox {
public:
  virtual bool showMsgBox(int flags,const char* title, const char* text) {
    return true;
  }
};

static UPG upg;
static UMB msg;

VNCClient::VNCClient(RDPSession* rdp_)
  : hasInitialized(false), pb(nullptr), rdp(rdp_), needStop(false), hasStopped(false), hasStarted(false) {
  rfb::SecurityClient::setDefaults();
  CSecurity::upg = &upg;
  CSecurityTLS::msg = &msg;
}

VNCClient::~VNCClient() {
}

bool VNCClient::init(const char* host, uint16_t port) {
  socket.reset(new network::TcpSocket(host, port));
  if (!socket) {
    return false;
  }
  setServerName(host);
  setStreams(&socket->inStream(), &socket->outStream());
  initialiseProtocol();

  return true;
}

void VNCClient::initDone() {
  PixelFormat format(32, 24, false, true, 255, 255, 255, 16, 8, 0);
  setPF(format);
  pb = new ManagedPixelBuffer(format, server.width(), server.height());
  setFramebuffer(pb);
  hasInitialized = true;
}

void VNCClient::setCursor(int width, int height, const rfb::Point& hotspot, const rdr::U8* data) {

}

void VNCClient::setCursorPos(const rfb::Point& pos) {

}

void VNCClient::setColourMapEntries(int firstColour, int nColours, rdr::U16* rgbs) {

}

void VNCClient::bell() {

}

bool VNCClient::waitInit() {
  while (true) {
    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    int fd = socket->getFd();
    FD_SET(fd, &rfds);
    if (socket->outStream().hasBufferedData()) {
      FD_SET(fd, &wfds);
    }
    int n = select(FD_SETSIZE, &rfds, &wfds, 0, NULL);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      } else {
        return false;
      }
    }
    socket->outStream().flush();
    socket->outStream().cork(true);
    while (processMsg()) {
      Timer::checkTimeouts();
    }
    socket->outStream().cork(false);
    socket->outStream().flush();
    if (hasInitialized) {
      return true;
    }
  }
  return true;
}

void VNCClient::startThread() {
  thread_.reset(new thread([this] {
    eventLoop();
  }));
}

void VNCClient::eventLoop() {
  hasStarted = true;
  while (true) {
    cerr << "test2\n";
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000;
    if (needStop) {
      hasStopped = true;
      return;
    }
    fd_set rfds, wfds;
    int fd;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    {
      lock_guard<mutex> lock(rdp->getMutex());
      fd = socket->getFd();
      FD_SET(fd, &rfds);
      if (socket->outStream().hasBufferedData()) {
        FD_SET(fd, &wfds);
      }
    }
    int n = select(FD_SETSIZE, &rfds, &wfds, 0, &tv);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      } else {
        return;
      }
    }
    {
      lock_guard<mutex> lock(rdp->getMutex());
      socket->outStream().flush();
      socket->outStream().cork(true);
      while (processMsg()) {
        Timer::checkTimeouts();
      }
      socket->outStream().cork(false);
      socket->outStream().flush();
    }
  }
}

void VNCClient::stop() {
  if (!hasStarted) {
    return;
  }
  needStop = true;
  while (!hasStopped);
}

int VNCClient::width() {
  return server.width();
}

int VNCClient::height() {
  return server.height();
}

void VNCClient::framebufferUpdateStart() {
  CConnection::framebufferUpdateStart();
  //cerr << "start frame\n";
  rdp->startFrame();
}

void VNCClient::framebufferUpdateEnd() {
  CConnection::framebufferUpdateEnd();
  //cerr << "end frame\n";
  rdp->endFrame();
}

bool VNCClient::dataRect(const Rect& r, int encoding) {
  if (!CConnection::dataRect(r, encoding)) {
    return false;
  }
  int stride;
  uint8_t* buffer = pb->getBufferRW(r, &stride);
  return rdp->drawRect(r, buffer, stride);
}

int VNCClient::getFd() {
  return socket->getFd();
}

void VNCClient::process() {
  while (processMsg()) {
    Timer::checkTimeouts();
  }
  socket->outStream().flush();
}