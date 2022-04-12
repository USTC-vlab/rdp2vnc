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
// RDPClient.h
//

#ifndef __RDPCLIENT_H__
#define __RDPCLIENT_H__

#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <inttypes.h>
#include <unistd.h>

#include <freerdp/freerdp.h>
#include <freerdp/constants.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/utils/signal.h>

#include <freerdp/client/file.h>
#include <freerdp/client/cmdline.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/client/disp.h>
#include <freerdp/client/channels.h>
#include <freerdp/channels/channels.h>

#include <rfb/Rect.h>
#include <rfb/ScreenSet.h>
#include <rfb/Configuration.h>

struct RDPCursor;
class RDPDesktop;
class RDPPointerImpl;
class RDPClient
{
public:
  RDPClient(int argc, char** argv, bool& stopSignal_);
  ~RDPClient();
  bool init(char* domain, char* username, char* password, int width, int height);
  bool start();
  bool stop();
  bool waitConnect();
  void processsEvents();
  bool startThread();
  void setRDPDesktop(RDPDesktop* desktop);
  int width();
  int height();
  rdr::U8* getBuffer();
  void pointerEvent(const rfb::Point& pos, int buttonMask);
  void keyEvent(rdr::U32 keysym, rdr::U32 xtcode, bool down);
  void handleClipboardRequest();
  void handleClipboardAnnounce(bool available);
  void handleClipboardData(const char* data);
  unsigned int setScreenLayout(int fbWidth, int fbHeight, const rfb::ScreenSet& layout);
  std::mutex &getMutex();
private:
  friend class RDPDesktop;
  static BOOL rdpBeginPaint(rdpContext* context);
  static BOOL rdpEndPaint(rdpContext* context);
  static BOOL rdpPreConnect(freerdp* instance);
  static BOOL rdpPostConnect(freerdp* instance);
  static void rdpPostDisconnect(freerdp* instance);
  static BOOL rdpClientNew(freerdp* instance, rdpContext* context);
  static BOOL rdpPointerNew(rdpContext* context, rdpPointer* pointer);
  static void rdpPointerFree(rdpContext* context, rdpPointer* pointer);
  static BOOL rdpPointerSet(rdpContext* context, const rdpPointer* pointer);
  static BOOL rdpPointerSetPosition(rdpContext* context, UINT32 x, UINT32 y);
  static BOOL rdpDesktopResize(rdpContext* context);
  static BOOL rdpPlaySound(rdpContext* context, const PLAY_SOUND_UPDATE* playSound);
  static void rdpChannelConnected(void* context, ChannelConnectedEventArgs* e);
  static void rdpChannelDisconnected(void* context, ChannelDisconnectedEventArgs* e);
  static UINT rdpCliprdrMonitorReady(CliprdrClientContext* context, const CLIPRDR_MONITOR_READY* monitorReady);
  static UINT rdpCliprdrServerCapabilities(CliprdrClientContext* context, const CLIPRDR_CAPABILITIES* capabilities);
  static UINT rdpCliprdrServerFormatList(CliprdrClientContext* context, const CLIPRDR_FORMAT_LIST* formatList);
  static UINT rdpCliprdrServerFormatListResponse(CliprdrClientContext* context,
                                                 const CLIPRDR_FORMAT_LIST_RESPONSE* formatListResponse);
  static UINT rdpCliprdrServerFormatDataRequest(CliprdrClientContext* context,
                                                const CLIPRDR_FORMAT_DATA_REQUEST* formatDataRequest);
  static UINT rdpCliprdrServerFormatDataResponse(CliprdrClientContext* context,
                                                const CLIPRDR_FORMAT_DATA_RESPONSE* formatDataResponse);
  static UINT rdpDisplayControlCaps(DispClientContext* context, UINT32 maxNumMonitors,
                                    UINT32 maxMonitorAreaFactorA, UINT32 maxMonitorAreaFacotrB);
  bool beginPaint();
  bool endPaint();
  bool preConnect();
  bool postConnect();
  void postDisconnect();
  bool pointerSet(RDPPointerImpl* pointer);
  bool pointerSetPosition(uint32_t x, uint32_t y);
  bool desktopResize();
  bool playSound(const PLAY_SOUND_UPDATE* playSound);
  UINT cliprdrMonitorReady(const CLIPRDR_MONITOR_READY* monitorReady);
  UINT cliprdrServerCapabilities(const CLIPRDR_CAPABILITIES* capabilities);
  UINT cliprdrServerFormatList(const CLIPRDR_FORMAT_LIST* formatList);
  UINT cliprdrServerFormatListResponse(const CLIPRDR_FORMAT_LIST_RESPONSE* formatListResponse);
  UINT cliprdrServerFormatDataRequest(const CLIPRDR_FORMAT_DATA_REQUEST* formatDataRequest);
  UINT cliprdrServerFormatDataResponse(const CLIPRDR_FORMAT_DATA_RESPONSE* formatDataResponse);
  UINT cliprdrSendClientFormatList();
  UINT cliprdrSendFormatList(const CLIPRDR_FORMAT* formats, int numFormats);
  UINT cliprdrSendDataResponse(const uint8_t* data, size_t size);
  UINT displayControlCaps(uint32_t maxNumMonitors, uint32_t maxMonitorAreaFactorA, uint32_t maxMonitorAreaFacotrB);

  void channelConnected(ChannelConnectedEventArgs* e);
  void channelDisconnected(ChannelDisconnectedEventArgs* e);
  void eventLoop();
  void sendPendingPointer();

  int argc;
  char** argv;
  bool& stopSignal;
  rdpContext* context;
  freerdp* instance;
  RDPDesktop* desktop;
  CliprdrClientContext* cliprdrContext;
  DispClientContext* dispContext;
  bool hasConnected;
  bool hasSentCliprdrFormats;
  int oldButtonMask;
  uint32_t cliprdrRequestedFormatId;
  std::unique_ptr<std::thread> thread_;
  std::shared_ptr<RDPCursor> lastCursor;
  std::mutex mutexVNC;
  std::mutex mutexCliprdr;
  std::unordered_set<uint32_t> pressedKeys;
  std::unordered_set<uint32_t> combinedKeys;
  bool hasCapsLocked;
  bool hasSyncedCapsLocked;
  bool hasAnnouncedClipboard;
  bool isClientClipboardAvailable;
  bool hasClientRequestedClipboard;
  bool hasReceivedDisplayControlCaps;
  bool hasPendingChangeSize;
  bool hasPendingPointer;
  int maxNumMonitors;
  int maxMonitorAreaFactorA;
  int maxMonitorAreaFactorB;
  int64_t lastProcessTime;
  int64_t lastChangeSizeTime;
};

#endif // __RDPCLIENT_H__

