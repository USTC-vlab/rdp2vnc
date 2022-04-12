#include <memory>
#include <iostream>
#include <mutex>
#include <thread>
#include <ctime>
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
#include <rfb/CMsgWriter.h>

#include "vnc2rdp/RDPSession.h"
#include "vnc2rdp/VNCClient.h"

using namespace std;
using namespace rfb;

RDPSession::RDPSession(freerdp_peer* peer_)
  : frameId(0), peer(peer_), context(nullptr), rfx(nullptr), nsc(nullptr),
    stream(nullptr), has_activated(false) {}

RDPSession::~RDPSession() {
  freerdp_peer_context_free(peer);
  freerdp_peer_free(peer);
}

bool RDPSession::init() {
  peer->ContextExtra = this;
  peer->ContextSize = sizeof(RDPContext);
  peer->ContextNew = rdpContextNew;
  peer->ContextFree = rdpContextFree;
  if (!freerdp_peer_context_new(peer)) {
    return false;
  }
  peer->settings->CertificateFile = strdup("server.crt");
  peer->settings->PrivateKeyFile = strdup("server.key");
  peer->settings->RdpKeyFile = strdup("server.key");
  peer->settings->RdpSecurity = true;
  peer->settings->TlsSecurity = true;
  peer->settings->NlaSecurity = false;
  peer->settings->EncryptionLevel = ENCRYPTION_LEVEL_CLIENT_COMPATIBLE;
  peer->settings->RemoteFxCodec = true;
  peer->settings->NSCodec = true;
  peer->settings->ColorDepth = 32;
  peer->settings->SuppressOutput = true;
  peer->settings->RefreshRect = true;
  peer->PostConnect = rdpPostConnect;
  peer->Activate = rdpActivate;
  peer->input->KeyboardEvent = rdpKeyboardEvent;
  peer->update->RefreshRect = rdpRefreshRect;
  peer->Initialize(peer);
  return true;
}

void RDPSession::run() {
  HANDLE handles[32];
  DWORD numHandles;
  while (true) {
    //cerr << "test1\n";
    {
      //lock_guard<mutex> lock(mutex_);
      numHandles = peer->GetEventHandles(peer, handles, 30);
      if (numHandles == 0) {
        break;
      }
      handles[numHandles++] = WTSVirtualChannelManagerGetEventHandle(vcm);
    }
    if (vnc) {
      HANDLE vncHandle = CreateFileDescriptorEvent(nullptr, false, false, vnc->getFd(), WINPR_FD_READ);
      handles[numHandles++] = vncHandle;
    }
    if (WaitForMultipleObjects(numHandles, handles, false, 10000) == WAIT_FAILED) {
      break;
    }
    if (vnc) {
      vnc->process();
    }
    {
      //lock_guard<mutex> lock(mutex_);
      if (!peer->CheckFileDescriptor(peer)) {
        break;
      }
      if (!WTSVirtualChannelManagerCheckFileDescriptor(vcm)) {
        break;
      }
    }
  }
  peer->Disconnect(peer);
  if (vnc) {
    vnc->stop();
  }
}

std::mutex& RDPSession::getMutex() {
  return mutex_;
}

BOOL RDPSession::rdpContextNew(freerdp_peer* peer_, rdpContext* ctx) {
  RDPSession* session = (RDPSession *)peer_->ContextExtra;
  session->context = ctx;
  return session->contextNew();
}

void RDPSession::rdpContextFree(freerdp_peer* peer_, rdpContext* ctx) {
  RDPSession* session = (RDPSession *)peer_->ContextExtra;
  session->contextFree();
}

BOOL RDPSession::rdpPostConnect(freerdp_peer* peer_) {
  RDPSession* session = (RDPSession *)peer_->ContextExtra;
  return session->postConnect();
}

BOOL RDPSession::rdpActivate(freerdp_peer* peer_) {
  RDPSession* session = (RDPSession *)peer_->ContextExtra;
  return session->activate();
}

BOOL RDPSession::rdpKeyboardEvent(rdpInput* input, UINT16 flags, UINT16 code) {
  RDPSession* session = ((RDPContext *)input->context)->session;
  return session->keyboardEvent(flags, code);
}

BOOL RDPSession::rdpRefreshRect(rdpContext* context, BYTE count, const RECTANGLE_16* areas) {
  RDPSession* session = ((RDPContext *)context)->session;
  return session->refreshRect(count, areas);
}

bool RDPSession::contextNew() {
  ((RDPContext *)context)->session = this;
  rfx = rfx_context_new(true);
  if (!rfx) {
    return false;
  }
  if (!rfx_context_reset(rfx, 1024, 768)) {
    return false;
  }
  rfx->mode = RLGR3;
  rfx_context_set_pixel_format(rfx, PIXEL_FORMAT_BGRA32);
  nsc = nsc_context_new();
  if (!nsc) {
    return false;
  }
  if (!nsc_context_set_parameters(nsc, NSC_COLOR_FORMAT, PIXEL_FORMAT_BGRA32)) {
    return false;
  }
  stream = Stream_New(nullptr, 65536);
  if (!stream) {
    return false;
  }
  vcm = WTSOpenServerA((LPSTR)context);
  if (!vcm || vcm == INVALID_HANDLE_VALUE) {
    return false;
  }
  return true;
}

void RDPSession::contextFree() {
  if (context) {
    Stream_Free(stream, true);
    rfx_context_free(rfx);
    nsc_context_free(nsc);
    WTSCloseServer(vcm);
  }
}

bool RDPSession::postConnect() {
  vnc.reset(new VNCClient(this));
  if (!vnc->init("localhost", 5900)) {
    return false;
  }
  if (!vnc->waitInit()) {
    return false;
  }
  if (!rfx_context_reset(rfx, vnc->width(), vnc->height())) {
    return false;
  }
  peer->settings->DesktopWidth = rfx->width;
  peer->settings->DesktopHeight = rfx->height;
  peer->update->DesktopResize(context);
  return true;
}

bool RDPSession::activate() {
  peer->settings->CompressionLevel = PACKET_COMPR_TYPE_RDP61;
  has_activated = true;
  vnc->refreshFramebuffer();
  //vnc->startThread();
  //{
  //  lock_guard<mutex> lock(mutex_);
  //  vnc->refreshFramebuffer();
  //}
  return true;
}

bool RDPSession::keyboardEvent(uint16_t flags, uint16_t code) {
  if (vnc) {
    vnc->writer()->writeKeyEvent('a', code, flags & KBD_FLAGS_DOWN);
  }
  return true;
}

bool RDPSession::refreshRect(BYTE count, const RECTANGLE_16* areas) {
  return true;
}

void RDPSession::startFrame() {
  if (!has_activated) {
    return;
  }
  rdpUpdate* update = peer->update;
  SURFACE_FRAME_MARKER fm = {0};
  fm.frameAction = SURFACECMD_FRAMEACTION_BEGIN;
  fm.frameId = frameId;
  update->SurfaceFrameMarker(context, &fm);
}

void RDPSession::endFrame() {
  if (!has_activated) {
    return;
  }
  rdpUpdate* update = peer->update;
  SURFACE_FRAME_MARKER fm = {0};
  fm.frameAction = SURFACECMD_FRAMEACTION_END;
  fm.frameId = frameId;
  update->SurfaceFrameMarker(context, &fm);
  ++frameId;
}

bool RDPSession::drawRect(const rfb::Rect& r, const uint8_t* data, int stride) {
  if (!has_activated || !data) {
    return true;
  }
  if (!peer->settings->RemoteFxCodec) {
    return true;
  }
  cerr << "testrfx\n";
  Stream_Clear(stream);
  Stream_SetPosition(stream, 0);
  RFX_RECT rect;
  SURFACE_BITS_COMMAND cmd = { 0 };
  rect.x = 0;
  rect.y = 0;
  int width = r.width();
  int height = r.height();
  //cerr << rect.x << " " << rect.y << " " << width << " " << height << endl;
  rect.width = width;
  rect.height = height;
  if (peer->settings->RemoteFxCodec) {
    if (!rfx_compose_message(rfx, stream, &rect, 1, (uint8_t *)data, width, height, stride * 4)) {
      return true;
    }
    cmd.bmp.codecID = peer->settings->RemoteFxCodecId;
    cmd.cmdType = CMDTYPE_STREAM_SURFACE_BITS;
  }
  cmd.destLeft = r.tl.x;
  cmd.destTop = r.tl.y;
  cmd.destRight = r.br.x;
  cmd.destBottom = r.br.y;
  cmd.bmp.bpp = 32;
  cmd.bmp.flags = 0;
  cmd.bmp.width = width;
  cmd.bmp.height = height;
  cmd.bmp.bitmapDataLength = Stream_GetPosition(stream);
  cmd.bmp.bitmapData = Stream_Buffer(stream);
  peer->update->SurfaceBits(context, &cmd);
  return true;
}