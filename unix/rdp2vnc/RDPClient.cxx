/* Copyright 2021 Dinglan Peng
 *    
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

//
// RDPClient.cxx
//

#include <iostream>
#include <vector>
#include <memory>
#include <thread>
#include <string>
#include <unordered_map>
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/time.h>

#include <freerdp/freerdp.h>
#include <freerdp/constants.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/gdi/gfx.h>
#include <freerdp/utils/signal.h>

#include <freerdp/client/file.h>
#include <freerdp/client/cmdline.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/client/disp.h>
#include <freerdp/client/channels.h>
#include <freerdp/channels/channels.h>

#include <winpr/crt.h>
#include <winpr/synch.h>

#include <xkbcommon/xkbcommon.h>
#include <utf8cpp/utf8.h>

#include <rfb/PixelFormat.h>
#include <rfb/Rect.h>
#include <rfb/Pixel.h>
#include <rfb/util.h>
#include <rfb/ScreenSet.h>
#include <rfb/LogWriter.h>

#include <rdp2vnc/RDPClient.h>
#include <rdp2vnc/RDPDesktop.h>
#include <rdp2vnc/RDPCursor.h>
#include <rdp2vnc/key.h>

using namespace std;
using namespace rfb;

static rfb::LogWriter vlog("RDPClient");

static int64_t getMSTimestamp() {
  timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

struct RDPContext {
  rdpContext context;
  RDPClient* client;
};

struct RDPPointerImpl {
  RDPPointerImpl(rdpPointer* pointer_)
    : pointer(pointer_), buffer(NULL) {}
  bool init(rdpGdi* gdi) {
    if (!gdi) {
      return false;
    }
    uint32_t cursorFormat = PIXEL_FORMAT_RGBA32;
    x = pointer->xPos;
    y = pointer->yPos;
    width = pointer->width;
    height = pointer->height;
    size = width * height * GetBytesPerPixel(cursorFormat);
    buffer = new uint8_t[size];
    if (!buffer) {
      return false;
    }
    if (!freerdp_image_copy_from_pointer_data(
          buffer, cursorFormat, 0, 0, 0, width, height,
          pointer->xorMaskData, pointer->lengthXorMask, pointer->andMaskData,
          pointer->lengthAndMask, pointer->xorBpp, &gdi->palette)) {
      return false;
    }
    return true;
  }
  ~RDPPointerImpl() {
    if (buffer) {
      delete[] buffer;
    }
  }
  rdpPointer* pointer;
  uint8_t* buffer;
  size_t size;
  int x;
  int y;
  int width;
  int height;
};

struct RDPPointer {
  rdpPointer pointer;
  RDPPointerImpl* impl;
};

BOOL RDPClient::rdpBeginPaint(rdpContext* context) {
  RDPContext* ctx = (RDPContext *)context;
  return ctx->client->beginPaint();
}

BOOL RDPClient::rdpEndPaint(rdpContext* context) {
  RDPContext* ctx = (RDPContext *)context;
  return ctx->client->endPaint();
}

BOOL RDPClient::rdpPreConnect(freerdp* instance) {
  RDPContext* ctx = (RDPContext *)instance->context;
  return ctx->client->preConnect();
}

BOOL RDPClient::rdpPostConnect(freerdp* instance) {
  RDPContext* ctx = (RDPContext *)instance->context;
  return ctx->client->postConnect();
}

void RDPClient::rdpPostDisconnect(freerdp* instance) {
  RDPContext* ctx = (RDPContext *)instance->context;
  ctx->client->postDisconnect();
}

BOOL RDPClient::rdpClientNew(freerdp* instance, rdpContext* context) {
  if (!instance || !context) {
    return FALSE;
  }

  instance = context->instance;
  instance->PreConnect = rdpPreConnect;
  instance->PostConnect = rdpPostConnect;
  instance->PostDisconnect = rdpPostDisconnect;
  return TRUE;
}

BOOL RDPClient::rdpPointerNew(rdpContext* context, rdpPointer* pointer) {
  if (!context || !pointer) {
    return FALSE;
  }
  //RDPContext* ctx = (RDPContext *)context;
  RDPPointer* ptr = (RDPPointer *)pointer;
  ptr->impl = new RDPPointerImpl(pointer);
  if (!ptr->impl) {
    return FALSE;
  }
  return ptr->impl->init(context->gdi);
}

void RDPClient::rdpPointerFree(rdpContext* context, rdpPointer* pointer) {
  if (!context || !pointer) {
    return;
  }
  //RDPContext* ctx = (RDPContext *)context;
  RDPPointer* ptr = (RDPPointer *)pointer;
  if (ptr->impl) {
    delete ptr->impl;
  }
}

BOOL RDPClient::rdpPointerSet(rdpContext* context, const rdpPointer* pointer) {
  if (!context || !pointer) {
    return FALSE;
  }
  RDPContext* ctx = (RDPContext *)context;
  RDPPointer* ptr = (RDPPointer *)pointer;
  if (!ctx->client || !ptr->impl) {
    return FALSE;
  }
  return ctx->client->pointerSet(ptr->impl);
}

BOOL RDPClient::rdpPointerSetPosition(rdpContext* context, UINT32 x, UINT32 y) {
  if (!context) {
    return FALSE;
  }
  RDPContext* ctx = (RDPContext *)context;
  return ctx->client->pointerSetPosition(x, y);
}

BOOL RDPClient::rdpDesktopResize(rdpContext* context) {
  if (!context) {
    return FALSE;
  }
  RDPContext* ctx = (RDPContext *)context;
  return ctx->client->desktopResize();
}

BOOL RDPClient::rdpPlaySound(rdpContext* context, const PLAY_SOUND_UPDATE* playSound) {
  if (!context) {
    return FALSE;
  }
  RDPContext* ctx = (RDPContext *)context;
  return ctx->client->playSound(playSound);
}

void RDPClient::rdpChannelConnected(void* context, ChannelConnectedEventArgs* e) {
  if (!context) {
    return;
  }
  RDPContext* ctx = (RDPContext *)context;
  ctx->client->channelConnected(e);
}

void RDPClient::rdpChannelDisconnected(void* context, ChannelDisconnectedEventArgs* e) {
  if (!context) {
    return;
  }
  RDPContext* ctx = (RDPContext *)context;
  ctx->client->channelDisconnected(e);
}

UINT RDPClient::rdpCliprdrMonitorReady(CliprdrClientContext* context,
  const CLIPRDR_MONITOR_READY* monitorReady)
{
  if (!context) {
    return CHANNEL_RC_OK;
  }
  RDPClient* client = (RDPClient *)context->custom;
  return client->cliprdrMonitorReady(monitorReady);
}

UINT RDPClient::rdpCliprdrServerCapabilities(CliprdrClientContext* context,
  const CLIPRDR_CAPABILITIES* capabilities)
{
  if (!context) {
    return CHANNEL_RC_OK;
  }
  RDPClient* client = (RDPClient *)context->custom;
  return client->cliprdrServerCapabilities(capabilities);
}

UINT RDPClient::rdpCliprdrServerFormatList(CliprdrClientContext* context,
  const CLIPRDR_FORMAT_LIST* formatList)
{
  if (!context) {
    return CHANNEL_RC_OK;
  }
  RDPClient* client = (RDPClient *)context->custom;
  return client->cliprdrServerFormatList(formatList);
}

UINT RDPClient::rdpCliprdrServerFormatListResponse(CliprdrClientContext* context,
  const CLIPRDR_FORMAT_LIST_RESPONSE* formatListResponse)
{
  if (!context) {
    return CHANNEL_RC_OK;
  }
  RDPClient* client = (RDPClient *)context->custom;
  return client->cliprdrServerFormatListResponse(formatListResponse);
}

UINT RDPClient::rdpCliprdrServerFormatDataRequest(CliprdrClientContext* context,
  const CLIPRDR_FORMAT_DATA_REQUEST* formatDataRequest)
{
  if (!context) {
    return CHANNEL_RC_OK;
  }
  RDPClient* client = (RDPClient *)context->custom;
  return client->cliprdrServerFormatDataRequest(formatDataRequest);
}

UINT RDPClient::rdpCliprdrServerFormatDataResponse(CliprdrClientContext* context,
  const CLIPRDR_FORMAT_DATA_RESPONSE* formatDataResponse)
{
  if (!context) {
    return CHANNEL_RC_OK;
  }
  RDPClient* client = (RDPClient *)context->custom;
  return client->cliprdrServerFormatDataResponse(formatDataResponse);
}

UINT RDPClient::rdpDisplayControlCaps(DispClientContext* context, UINT32 maxNumMonitors,
  UINT32 maxMonitorAreaFactorA, UINT32 maxMonitorAreaFactorB) {
  if (!context) {
    return CHANNEL_RC_OK;
  }
  RDPClient* client = (RDPClient *)context->custom;
  return client->displayControlCaps(maxNumMonitors, maxMonitorAreaFactorA, maxMonitorAreaFactorB);
}

bool RDPClient::beginPaint() {
  return true;
}

bool RDPClient::endPaint() {
  rdpGdi* gdi = context->gdi;
  if (gdi->primary->hdc->hwnd->invalid->null) {
    return true;
  }
  //hasPendingChangeSize = false;
  int ninvalid = gdi->primary->hdc->hwnd->ninvalid;
  HGDI_RGN cinvalid = gdi->primary->hdc->hwnd->cinvalid;
  for (int i = 0; i < ninvalid; ++i) {
    int x = cinvalid[i].x;
    int y = cinvalid[i].y;
    int w = cinvalid[i].w;
    int h = cinvalid[i].h;
    if (desktop && desktop->server) {
      try {
        desktop->server->add_changed(Region(Rect(x, y, x + w, y + h)));
      } catch (rdr::Exception& e) {
        vlog.error("Add changed: %s", e.str());
      }
    }
  }
  gdi->primary->hdc->hwnd->invalid->null = TRUE;
  gdi->primary->hdc->hwnd->ninvalid = 0;
  return true;
}

bool RDPClient::preConnect() {
  //rdpSettings* settings;
  //settings = instance->settings;
  PubSub_SubscribeChannelConnected(instance->context->pubSub, rdpChannelConnected);
  PubSub_SubscribeChannelDisconnected(instance->context->pubSub, rdpChannelDisconnected);
  if (!freerdp_client_load_addins(instance->context->channels, instance->settings)) {
    return false;
  }
  return true;
}

bool RDPClient::postConnect() {
  // Init GDI
  if (!gdi_init(instance, PIXEL_FORMAT_BGRX32)) {
    return false;
  }

  // Register pointer
  rdpPointer pointer;
  memset(&pointer, 0, sizeof(pointer));
  pointer.size = sizeof(RDPPointer);
  pointer.New = rdpPointerNew;
  pointer.Free = rdpPointerFree;
  pointer.Set = rdpPointerSet;
  graphics_register_pointer(context->graphics, &pointer);

  instance->update->BeginPaint = rdpBeginPaint;
  instance->update->EndPaint = rdpEndPaint;
  instance->update->DesktopResize = rdpDesktopResize;
  instance->update->PlaySound = rdpPlaySound;
  hasConnected = true;
  return true;
}

void RDPClient::postDisconnect() {
  gdi_free(instance);
}

bool RDPClient::pointerSet(RDPPointerImpl* pointer) {
  if (!pointer->buffer) {
    return false;
  }
  int width = pointer->width;
  int height = pointer->height;
  int x = pointer->x;
  int y = pointer->y;
  Point hotspot(x, y);
  lastCursor.reset(new RDPCursor(pointer->buffer, pointer->size, width, height, x, y));
  if (hasPendingChangeSize) {
    hasPendingPointer = true;
  } else {
    if (desktop && desktop->server) {
      hasPendingPointer = false;
      try {
        desktop->server->setCursor(width, height, hotspot, pointer->buffer);
      } catch (rdr::Exception& e) {
        vlog.error("Set cursor: %s", e.str());
      }
    }
  }
  if (desktop && !desktop->server) {
    desktop->setFirstCursor(lastCursor);
  }
  return true;
}

bool RDPClient::pointerSetPosition(uint32_t x, uint32_t y) {
  if (lastCursor) {
    lastCursor->posX = x;
    lastCursor->posY = y;
  }
  if (hasPendingChangeSize) {
    hasPendingPointer = true;
  } else {
    if (desktop && desktop->server) {
      hasPendingPointer = false;
      try {
        desktop->server->setCursorPos(Point(x, y), false);
      } catch (rdr::Exception& e) {
        vlog.error("Set cursor position: %s", e.str());
      }
    }
  }
  return true;
}

bool RDPClient::desktopResize() {
  if (!gdi_resize(context->gdi, context->settings->DesktopWidth, context->settings->DesktopHeight)) {
    return false;
  }
  if (!desktop) {
    return true;
  }
  if (!desktop->resize()) {
    return false;
  }
  hasPendingChangeSize = true;
  lastChangeSizeTime = getMSTimestamp();
  return true;
}

bool RDPClient::playSound(const PLAY_SOUND_UPDATE* playSound) {
  if (desktop && desktop->server) {
    try {
      desktop->server->bell();
    } catch (rdr::Exception& e) {
      vlog.error("Bell: %s", e.str());
    }
  }
  return true;
}

void RDPClient::channelConnected(ChannelConnectedEventArgs* e) {
  if (strcmp(e->name, RDPGFX_DVC_CHANNEL_NAME) == 0) {
    gdi_graphics_pipeline_init(context->gdi, (RdpgfxClientContext *)e->pInterface);
  } else if (strcmp(e->name, CLIPRDR_SVC_CHANNEL_NAME) == 0) {
    cliprdrContext = (CliprdrClientContext *)e->pInterface;
    cliprdrContext->custom = (void *)this;
    cliprdrContext->MonitorReady = rdpCliprdrMonitorReady;
    cliprdrContext->ServerCapabilities = rdpCliprdrServerCapabilities;
    cliprdrContext->ServerFormatList = rdpCliprdrServerFormatList;
    cliprdrContext->ServerFormatListResponse = rdpCliprdrServerFormatListResponse;
    cliprdrContext->ServerFormatDataRequest = rdpCliprdrServerFormatDataRequest;
    cliprdrContext->ServerFormatDataResponse = rdpCliprdrServerFormatDataResponse;
  } else if (strcmp(e->name, DISP_DVC_CHANNEL_NAME) == 0) {
    dispContext = (DispClientContext *)e->pInterface;
    dispContext->custom = (void *)this;
    dispContext->DisplayControlCaps = rdpDisplayControlCaps;
  }
}

void RDPClient::channelDisconnected(ChannelDisconnectedEventArgs* e) {
  if (strcmp(e->name, RDPGFX_DVC_CHANNEL_NAME) == 0) {
    gdi_graphics_pipeline_uninit(context->gdi, (RdpgfxClientContext *)e->pInterface);
  } else if (strcmp(e->name, CLIPRDR_SVC_CHANNEL_NAME) == 0) {
  }
}

UINT RDPClient::cliprdrMonitorReady(const CLIPRDR_MONITOR_READY* monitorReady) {
  lock_guard<mutex> lock(mutexCliprdr);
  UINT res;

  CLIPRDR_CAPABILITIES capabilities;
  CLIPRDR_GENERAL_CAPABILITY_SET generalCapabilitySet;
  capabilities.cCapabilitiesSets = 1;
  capabilities.capabilitySets = (CLIPRDR_CAPABILITY_SET *)&generalCapabilitySet;
  generalCapabilitySet.capabilitySetType = CB_CAPSTYPE_GENERAL;
  generalCapabilitySet.capabilitySetLength = 12;
  generalCapabilitySet.version = CB_CAPS_VERSION_2;
  generalCapabilitySet.generalFlags = CB_USE_LONG_FORMAT_NAMES;
  if ((res = cliprdrContext->ClientCapabilities(cliprdrContext, &capabilities)) != CHANNEL_RC_OK) {
    return res;
  }

  return CHANNEL_RC_OK;
}

UINT RDPClient::cliprdrServerCapabilities(const CLIPRDR_CAPABILITIES* capabilities) {
  return CHANNEL_RC_OK;
}

UINT RDPClient::cliprdrServerFormatList(const CLIPRDR_FORMAT_LIST* formatList) {
  if (desktop && desktop->server) {
    scoped_lock lock(mutexVNC, mutexCliprdr);
    try {
      desktop->server->announceClipboard(true);
    } catch (rdr::Exception& e) {
      vlog.error("Announce clipboard: %s", e.str());
    }
    hasAnnouncedClipboard = true;
  }
  CLIPRDR_FORMAT_LIST_RESPONSE formatListResponse;
  formatListResponse.msgType = CB_FORMAT_LIST_RESPONSE;
  formatListResponse.msgFlags = CB_RESPONSE_OK;
  formatListResponse.dataLen = 0;
  {
    lock_guard lock(mutexCliprdr);
    return cliprdrContext->ClientFormatListResponse(cliprdrContext, &formatListResponse);
  }
}

UINT RDPClient::cliprdrServerFormatListResponse(const CLIPRDR_FORMAT_LIST_RESPONSE* formatListResponse) {
  return CHANNEL_RC_OK;
}

UINT RDPClient::cliprdrServerFormatDataRequest(const CLIPRDR_FORMAT_DATA_REQUEST* formatDataRequest) {
  {
    lock_guard lock(mutexCliprdr);
    cliprdrRequestedFormatId = formatDataRequest->requestedFormatId;
    if (cliprdrRequestedFormatId != CF_RAW && cliprdrRequestedFormatId != CF_UNICODETEXT) {
      return cliprdrSendDataResponse(NULL, 0);
    }
    if (!isClientClipboardAvailable) {
      return cliprdrSendDataResponse(NULL, 0);
    }
  }
  if (desktop && desktop->server) {
    lock_guard<mutex> lock(mutexVNC);
    try {
      desktop->server->requestClipboard();
    } catch (rdr::Exception& e) {
      vlog.error("Request clipboard: %s", e.str());
    }
  }
  return CHANNEL_RC_OK;
}

UINT RDPClient::cliprdrServerFormatDataResponse(const CLIPRDR_FORMAT_DATA_RESPONSE* formatDataResponse) {
  string clipData;
  {
    lock_guard<mutex> lock(mutexCliprdr);
    if (formatDataResponse->msgFlags == CB_RESPONSE_FAIL) {
      return CHANNEL_RC_OK;
    }
    if (!hasClientRequestedClipboard) {
      return CHANNEL_RC_OK;
    }
    const uint8_t* data = formatDataResponse->requestedFormatData;
    uint32_t size = formatDataResponse->dataLen;
    u16string utf16Data((char16_t *)data, size / 2);
    string utf8Data = utf8::utf16to8(utf16Data);
    clipData.reserve(utf8Data.length());
    for (char c : utf8Data) {
      if (c != '\r') {
        clipData += c;
      }
    }
    hasClientRequestedClipboard = false;
  }
  if (desktop && desktop->server) {
    lock_guard<mutex> lock(mutexVNC);
    try {
      desktop->server->sendClipboardData(clipData.c_str());
    } catch (rdr::Exception& e) {
      vlog.error("Send clipboard data: %s", e.str());
    }
  }
  return CHANNEL_RC_OK;
}

UINT RDPClient::cliprdrSendDataResponse(const uint8_t* data, size_t size) {
  if (cliprdrRequestedFormatId < 0) {
    return CHANNEL_RC_OK;
  }
  CLIPRDR_FORMAT_DATA_RESPONSE response = {0};
  cliprdrRequestedFormatId = -1;
  response.msgFlags = data ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
  response.dataLen = size;
  response.requestedFormatData = data;
  return cliprdrContext->ClientFormatDataResponse(cliprdrContext, &response);
}

UINT RDPClient::cliprdrSendClientFormatList() {
  const int numFormats = 2;
  CLIPRDR_FORMAT formats[numFormats];
  memset(formats, 0, numFormats * sizeof(CLIPRDR_FORMAT));
  formats[0].formatId = CF_RAW;
  formats[1].formatId = CF_UNICODETEXT;
  return cliprdrSendFormatList(formats, numFormats);
}

UINT RDPClient::cliprdrSendFormatList(const CLIPRDR_FORMAT* formats, int numFormats) {
  CLIPRDR_FORMAT_LIST formatList = {0};
  formatList.msgFlags = CB_RESPONSE_OK;
  formatList.numFormats = numFormats;
  formatList.formats = (CLIPRDR_FORMAT *)formats;
  formatList.msgType = CB_FORMAT_LIST;

  if (!hasSentCliprdrFormats) {
    cliprdrSendDataResponse(NULL, 0);
    hasSentCliprdrFormats = true;
    return cliprdrContext->ClientFormatList(cliprdrContext, &formatList);
  }
  return CHANNEL_RC_OK;
}

UINT RDPClient::displayControlCaps(uint32_t maxNumMonitors,
  uint32_t maxMonitorAreaFactorA, uint32_t maxMonitorAreaFactorB) {
  this->maxNumMonitors = maxNumMonitors;
  this->maxMonitorAreaFactorA = maxMonitorAreaFactorA;
  this->maxMonitorAreaFactorB = maxMonitorAreaFactorB;
  hasReceivedDisplayControlCaps = true;
  return CHANNEL_RC_OK;
}

RDPClient::RDPClient(int argc_, char** argv_, bool& stopSignal_)
  : argc(argc_), argv(argv_), stopSignal(stopSignal_), context(NULL), instance(NULL), desktop(NULL),
    cliprdrContext(NULL), dispContext(NULL), hasConnected(false),
    hasSentCliprdrFormats(false), oldButtonMask(0), cliprdrRequestedFormatId(-1),
    hasCapsLocked(false), hasSyncedCapsLocked(false), hasAnnouncedClipboard(false),
    isClientClipboardAvailable(false), hasClientRequestedClipboard(false), hasReceivedDisplayControlCaps(false),
    hasPendingChangeSize(false), hasPendingPointer(false)
{
}

RDPClient::~RDPClient() {
  if (context) {
    freerdp_client_context_free(context);
  }
}

bool RDPClient::init(char* domain, char* username, char* password, int width, int height) {
  DWORD status;
  RDP_CLIENT_ENTRY_POINTS clientEntryPoints;
  memset(&clientEntryPoints, 0, sizeof(RDP_CLIENT_ENTRY_POINTS));
  clientEntryPoints.Version = RDP_CLIENT_INTERFACE_VERSION;
  clientEntryPoints.Size = sizeof(RDP_CLIENT_ENTRY_POINTS_V1);
  clientEntryPoints.ContextSize = sizeof(RDPContext);
  clientEntryPoints.ClientNew = rdpClientNew;
  clientEntryPoints.ClientFree = NULL;
  clientEntryPoints.ClientStart = NULL;
  clientEntryPoints.ClientStop = NULL;
  context = freerdp_client_context_new(&clientEntryPoints);

  if (!context) {
    return false;
  }

  RDPContext* ctxWithPointer = (RDPContext *)context;
  ctxWithPointer->client = this;
  instance = context->instance;

  status = freerdp_client_settings_parse_command_line(context->settings, argc, argv, false);
  if (status != 0) {
    freerdp_client_settings_command_line_status_print(context->settings, status, argc, argv);
    return false;
  }
  if (domain) {
    context->settings->Domain = domain;
  }
  if (username) {
    context->settings->Username = username;
  }
  if (password) {
    context->settings->Password = password;
  }
  if (width != -1) {
    context->settings->DesktopWidth = width;
  }
  if (height != -1) {
    context->settings->DesktopHeight = height;
  }
  return true;
}

bool RDPClient::start() {
  if (!context) {
    return false;
  }
  if (freerdp_client_start(context) != 0) {
    return false;
  }
  return true;
}

bool RDPClient::stop() {
  if (!context) {
    return false;
  }
  stopSignal = 1;
  if (thread_) {
    thread_->join();
  }
  if (instance) {
    freerdp_disconnect(instance);
  }
  if (freerdp_client_stop(context) != 0) {
    return false;
  }
  return true;
}

bool RDPClient::waitConnect() {
  if (!context || !instance) {
    return false;
  }
  if (!freerdp_connect(instance)) {
    return false;
  }
  HANDLE handles[64];
  while (!freerdp_shall_disconnect(instance)) {
    DWORD numHandles = freerdp_get_event_handles(context, &handles[0], 64);
    if (numHandles == 0) {
      freerdp_disconnect(instance);
      return false;
    }
    if (WaitForMultipleObjects(numHandles, handles, FALSE, 100) == WAIT_FAILED) {
      freerdp_disconnect(instance);
      return false;
    }
    if (!freerdp_check_event_handles(context)) {
      freerdp_disconnect(instance);
      return false;
    }
    if (hasConnected) {
      break;
    }
  }
  return true;
}

void RDPClient::processsEvents() {
  sendPendingPointer();
}

bool RDPClient::startThread() {
  thread_.reset(new std::thread([this] {
    eventLoop();
  }));
  return true;
}

void RDPClient::setRDPDesktop(RDPDesktop* desktop_) {
  desktop = desktop_;
  if (lastCursor) {
    desktop->setFirstCursor(lastCursor);
  }
}

void RDPClient::eventLoop() {
  HANDLE handles[64];
  DWORD numHandles;
  while (!freerdp_shall_disconnect(instance)) {
    {
      lock_guard<mutex> lock(mutexVNC);
      numHandles = freerdp_get_event_handles(context, handles, 64);
    }
    if (numHandles == 0) {
      return;
    }
    if (stopSignal) {
      return;
    }
    if (WaitForMultipleObjects(numHandles, handles, FALSE, 10000) == WAIT_FAILED) {
      return;
    }
    {
      lock_guard<mutex> lock(mutexVNC);
      if (!freerdp_check_event_handles(context)) {
        return;
      }
    }
  }
}

void RDPClient::sendPendingPointer() {
  if (hasPendingChangeSize) {
    int64_t now = getMSTimestamp();
    if (now - lastChangeSizeTime > 5000) {
      hasPendingChangeSize = false;
    }
  }
  if (hasPendingPointer && !hasPendingChangeSize) {
    if (lastCursor && desktop && desktop->server) {
      try {
        desktop->server->setCursor(lastCursor->width, lastCursor->height,
          Point(lastCursor->x, lastCursor->y), lastCursor->data);
      } catch (rdr::Exception& e) {
        vlog.error("Set cursor: %s", e.str());
      }
      if (lastCursor->posX >= 0 && lastCursor->posY >= 0) {
        try {
          desktop->server->setCursorPos(Point(lastCursor->posX, lastCursor->posY), false);
        } catch (rdr::Exception& e) {
          vlog.error("Set cursor position: %s", e.str());
        }
      }
    }
    hasPendingPointer = false;
  }
}

int RDPClient::width() {
  if (context && context->gdi && context->gdi->primary && context->gdi->primary->bitmap) {
    return context->gdi->primary->bitmap->width;
  } else {
    return -1;
  }
}

int RDPClient::height() {
  if (context && context->gdi && context->gdi->primary && context->gdi->primary->bitmap) {
    return context->gdi->primary->bitmap->height;
  } else {
    return -1;
  }
}

rdr::U8* RDPClient::getBuffer() {
  if (context && context->gdi && context->gdi->primary && context->gdi->primary->bitmap) {
    return (rdr::U8*)context->gdi->primary->bitmap->data;
  } else {
    return NULL;
  }
}

void RDPClient::pointerEvent(const Point& pos, int buttonMask) {
  uint16_t flags = 0;
  bool left = buttonMask & 1;
  bool middle = buttonMask & 2;
  bool right = buttonMask & 4;
  bool up = buttonMask & 8;
  bool down = buttonMask & 16;
  bool leftWheel = buttonMask & 32;
  bool rightWheel = buttonMask & 64;
  bool oldLeft = oldButtonMask & 1;
  bool oldMiddle = oldButtonMask & 2;
  bool oldRight = oldButtonMask & 4;
  bool oldUp = oldButtonMask & 8;
  bool oldDown = oldButtonMask & 16;
  bool oldLeftWheel = oldButtonMask & 32;
  bool oldRightWheel = oldButtonMask & 64;

  // Down and move
  if (left && !oldLeft) {
    flags |= PTR_FLAGS_DOWN | PTR_FLAGS_BUTTON1;
  }
  if (right && !oldRight) {
    flags |= PTR_FLAGS_DOWN | PTR_FLAGS_BUTTON2;
  }
  if (middle && !oldMiddle) {
    flags |= PTR_FLAGS_DOWN | PTR_FLAGS_BUTTON3;
  }
  if (flags == 0) {
    flags = PTR_FLAGS_MOVE;
  }
  freerdp_input_send_mouse_event(context->input, flags, pos.x, pos.y);

  // Release
  flags = 0;
  if (!left && oldLeft) {
    flags |= PTR_FLAGS_BUTTON1;
  }
  if (!right && oldRight) {
    flags |= PTR_FLAGS_BUTTON2;
  }
  if (!middle && oldMiddle) {
    flags |= PTR_FLAGS_BUTTON3;
  }
  if (flags != 0) {
    freerdp_input_send_mouse_event(context->input, flags, pos.x, pos.y);
  }

  // Vertical wheel
  flags = 0;
  if (up && !oldUp) {
    flags = PTR_FLAGS_WHEEL | (WheelRotationMask & 127);
  }
  if (down && !oldDown) {
    flags = PTR_FLAGS_WHEEL | PTR_FLAGS_WHEEL_NEGATIVE | (WheelRotationMask & (-127));
  }
  if (flags != 0) {
    freerdp_input_send_mouse_event(context->input, flags, 0, 0);
  }

  // Horizontal wheel
  flags = 0;
  if (leftWheel && !oldLeftWheel) {
    flags = PTR_FLAGS_HWHEEL | (WheelRotationMask & 127);
  }
  if (rightWheel && !oldRightWheel) {
    flags = PTR_FLAGS_HWHEEL | PTR_FLAGS_WHEEL_NEGATIVE | (WheelRotationMask & (-127));
  }
  if (flags != 0) {
    freerdp_input_send_mouse_event(context->input, flags, 0, 0);
  }
  oldButtonMask = buttonMask;
}

void RDPClient::keyEvent(rdr::U32 keysym, rdr::U32 xtcode, bool down) {
  if (down) {
    pressedKeys.insert(keysym);
  } else {
    pressedKeys.erase(keysym);
  }
  if (xtcode) {
    // Simple case
    bool pressedShift = pressedKeys.count(XKB_KEY_Shift_L) || pressedKeys.count(XKB_KEY_Shift_R);
    bool isUppercase = keysym >= 'A' && keysym <= 'Z';
    bool isLowercase = keysym >= 'a' && keysym <= 'z';
    bool newCapsLocked = hasCapsLocked;
    if ((isUppercase && !pressedShift) || (isLowercase && pressedShift)) {
      // The client sent a uppercase letter without pressing shift
      // or a lowercase letter with pressing shift,
      // which means the CapsLock is enabled
      newCapsLocked = true;
    } else if ((isUppercase && pressedShift) || (isLowercase && !pressedShift)) {
      newCapsLocked = false;
    }
    if (hasCapsLocked != newCapsLocked) {
      if (!hasSyncedCapsLocked) {
        // Only synchorinize CapsLock when we switch it for the first time
        freerdp_input_send_synchronize_event(context->input, newCapsLocked ? KBD_SYNC_CAPS_LOCK : 0);
        hasSyncedCapsLocked = true;
      } else {
        freerdp_input_send_keyboard_event_ex(context->input, true, RDP_SCANCODE_CAPSLOCK);
        freerdp_input_send_keyboard_pause_event(context->input);
        freerdp_input_send_keyboard_event_ex(context->input, false, RDP_SCANCODE_CAPSLOCK);
      }
    }
    hasCapsLocked = newCapsLocked;
    freerdp_input_send_keyboard_event_ex(context->input, down, xtcode);
  } else {
    // Advanced case
    auto it = keysymScancodeMap.find(keysym);
    if (it != keysymScancodeMap.end()) {
      uint32_t rdpCode = it->second;
      freerdp_input_send_keyboard_event_ex(context->input, down, rdpCode);
      return;
    }
    if (down && keysym >= 'A' && keysym <= 'Z') {
      if (pressedKeys.count(XKB_KEY_Control_L) ||
          pressedKeys.count(XKB_KEY_Control_R) ||
          pressedKeys.count(XKB_KEY_Alt_L) ||
          pressedKeys.count(XKB_KEY_Alt_R)) {
        combinedKeys.insert(keysym);
        auto it = keysymScancodeMap.find(keysym - 'A' + 'a');
        if (it != keysymScancodeMap.end()) {
          freerdp_input_send_keyboard_event_ex(context->input, down, it->second);
        }
        return;
      }
    }
    if (!down && keysym >= 'A' && keysym <= 'Z' && combinedKeys.count(keysym)) {
      auto it = keysymScancodeMap.find(keysym - 'A' + 'a');
      if (it != keysymScancodeMap.end()) {
        freerdp_input_send_keyboard_event_ex(context->input, down, it->second);
      }
      combinedKeys.erase(keysym);
      return;
    }
    uint16_t flags = down ? KBD_FLAGS_DOWN : KBD_FLAGS_RELEASE;
    uint32_t keyUTF32 = xkb_keysym_to_utf32(keysym);
    freerdp_input_send_unicode_keyboard_event(context->input, flags, keyUTF32);
  }
}

void RDPClient::handleClipboardRequest() {
  if (!cliprdrContext || !hasAnnouncedClipboard) {
    return;
  }
  lock_guard<mutex> lock(mutexCliprdr);
  CLIPRDR_FORMAT_DATA_REQUEST request = {0};
  request.requestedFormatId = CF_UNICODETEXT;
  cliprdrContext->ClientFormatDataRequest(cliprdrContext, &request);
  hasAnnouncedClipboard = false;
  hasClientRequestedClipboard = true;
}

void RDPClient::handleClipboardAnnounce(bool available) {
  if (!cliprdrContext) {
    return;
  }
  lock_guard<mutex> lock(mutexCliprdr);
  isClientClipboardAvailable = available;
  if (!available && cliprdrRequestedFormatId >= 0) {
    cliprdrSendDataResponse(NULL, 0);
    return;
  }
  if (available) {
    cliprdrSendClientFormatList();
  }
}

void RDPClient::handleClipboardData(const char* data) {
  if (!cliprdrContext) {
    return;
  }
  lock_guard<mutex> lock(mutexCliprdr);
  string utf8Data(data);
  u16string utf16Data = utf8::utf8to16(utf8Data);
  cliprdrSendDataResponse((const uint8_t *)utf16Data.c_str(), utf16Data.length() * 2 + 2);
  hasSentCliprdrFormats = false;
}

unsigned int RDPClient::setScreenLayout(int fbWidth, int fbHeight, const rfb::ScreenSet& layout) {
  // We only handle the simple case of one screen
  if (!dispContext || !hasReceivedDisplayControlCaps) {
    return rfb::resultProhibited;
  }
  int numMonitors = layout.num_screens();
  if (numMonitors != 1 || fbWidth > maxMonitorAreaFactorA || fbHeight > maxMonitorAreaFactorB) {
    return rfb::resultInvalid;
  }
  const Screen& screen = *layout.begin();
  if (!screen.dimensions.equals(Rect(0, 0, fbWidth, fbHeight))) {
    return rfb::resultInvalid;
  }
  DISPLAY_CONTROL_MONITOR_LAYOUT rdpLayout;
  rdpLayout.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
  rdpLayout.Left = 0;
  rdpLayout.Top = 0;
  rdpLayout.Width = fbWidth;
  rdpLayout.Height = fbHeight;
  rdpLayout.Orientation = ORIENTATION_LANDSCAPE;
  rdpLayout.PhysicalWidth = fbWidth / (96 / 25.4); // 96 DPI
  rdpLayout.PhysicalHeight = fbHeight / (96 / 25.4);
  rdpLayout.DesktopScaleFactor = 100;
  rdpLayout.DeviceScaleFactor = 100;
  if (dispContext->SendMonitorLayout(dispContext, 1, &rdpLayout) != CHANNEL_RC_OK) {
    return rfb::resultInvalid;
  }
  return rfb::resultInvalid;
}

std::mutex &RDPClient::getMutex() {
  return mutexVNC;
}