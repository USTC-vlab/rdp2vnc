#pragma once
#include <memory>
#include <mutex>
#include <thread>
#include <winpr/crt.h>
#include <winpr/ssl.h>
#include <winpr/synch.h>
#include <winpr/string.h>
#include <winpr/path.h>
#include <winpr/winsock.h>
#include <freerdp/freerdp.h>
#include <freerdp/listener.h>
#include <freerdp/codec/rfx.h>
#include <freerdp/codec/nsc.h>
#include <freerdp/channels/wtsvc.h>
#include <freerdp/server/audin.h>
#include <freerdp/server/rdpsnd.h>
#include <freerdp/server/encomsp.h>
#include <freerdp/channels/wtsvc.h>
#include <freerdp/channels/channels.h>
#include <freerdp/constants.h>
#include <freerdp/server/rdpsnd.h>
#include <rfb/Rect.h>

#include "vnc2rdp/VNCClient.h"

class RDPSession;
struct RDPContext {
  rdpContext context;
  RDPSession* session;
};

class RDPSession {
public:
  RDPSession(freerdp_peer* peer_);
  ~RDPSession();
  bool init();
  void run();
  std::mutex& getMutex();
private:
  friend class VNCClient;
  static BOOL rdpContextNew(freerdp_peer* peer_, rdpContext* ctx);
  static void rdpContextFree(freerdp_peer* peer_, rdpContext* ctx);
  static BOOL rdpPostConnect(freerdp_peer* peer_);
  static BOOL rdpActivate(freerdp_peer* peer_);
  static BOOL rdpKeyboardEvent(rdpInput* input, UINT16 flags, UINT16 code);
  static BOOL rdpRefreshRect(rdpContext* context, BYTE count, const RECTANGLE_16* areas);
  bool contextNew();
  void contextFree();
  bool postConnect();
  bool activate();
  bool keyboardEvent(uint16_t flags, uint16_t code);
  bool refreshRect(BYTE count, const RECTANGLE_16* areas);

  void startFrame();
  void endFrame();
  bool drawRect(const rfb::Rect& r, const uint8_t* data, int stride);
  int64_t frameId;
  freerdp_peer* peer;
  rdpContext* context;
  RFX_CONTEXT* rfx;
  NSC_CONTEXT* nsc;
  wStream* stream;
  HANDLE vcm;
  bool has_activated;
  std::unique_ptr<VNCClient> vnc;
  std::mutex mutex_;
};