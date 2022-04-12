/* Copyright Dinglan Peng
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

#ifndef __TERMINAL_H__
#define __TERMINAL_H__

#include <memory>
#include <unordered_set>
#include <functional>

#include <rfb/VNCServerST.h>
#include <rfb/SDesktop.h>
#include <unixcommon.h>

#include <rdp2vnc/Geometry.h>
#include <vterm.h>

class TerminalDesktop : public rfb::SDesktop
{
public:
  TerminalDesktop(Geometry* geometry);
  bool initTerminal(int lines, int cols);
  virtual ~TerminalDesktop();
  // -=- SDesktop interface
  virtual void start(rfb::VNCServer* vs);
  virtual void stop();
  virtual void terminate();
  bool isRunning();
  virtual void queryConnection(network::Socket* sock,
                               const char* userName);
  virtual void pointerEvent(const rfb::Point& pos, int buttonMask);
  virtual void keyEvent(rdr::U32 keysym, rdr::U32 xtcode, bool down);
  virtual void clientCutText(const char* str);
  virtual unsigned int setScreenLayout(int fb_width, int fb_height,
                                       const rfb::ScreenSet& layout);
  virtual void handleClipboardRequest();
  virtual void handleClipboardAnnounce(bool available);
  virtual void handleClipboardData(const char* data);
  int getInFd();
  int* getOutFds();
  VTerm* getTerminal();
  std::pair<int, int> getRequestedDesktopSize();
  const Geometry& getGeometry();
private:
  rfb::ScreenSet computeScreenLayout();
  static void terminalOutputCallback(const char* s, size_t len, void* user);
  static int screenDamage(VTermRect rect, void* user);
  static int screenMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user);
  void damage(VTermRect rect);
  void moveCursor(VTermPos pos, VTermPos oldpos, bool visible);
  void renderCursor(VTermPos pos);
  void renderCell(VTermPos pos, bool reverse = false);
  void terminalOutput(const char* s, size_t len);
  void renderGlyph(int x, int y, int width, uint32_t ch, uint32_t fg, uint32_t bg);
  rfb::Rect terminalPosToRFBRect(int x, int y, int width);
  rfb::Rect terminalLineToRFBRect(int line);
  rfb::Rect terminalRectToRFBRect(int x1, int x2, int y1, int y2);
  Geometry* geometry;
  rfb::ManagedPixelBuffer* pb;
  rfb::VNCServer* server;
  bool running;
  VTerm* vt;
  VTermScreen* vtScreen;
  VTermState* vtState;
  VTermScreenCallbacks screenCallbacks;
  VTermStateCallbacks stateCallbacks;
  int lines;
  int cols;
  int inPipeFd[2];
  int outPipeFd[2];
  std::unordered_set<uint32_t> pressedKeys;
  int requestedWidth;
  int requestedHeight;
  int defaultFGColor;
  int defaultBGColor;
};

bool runTerminal(TerminalDesktop* desktop, rfb::VNCServerST* server,
  std::list<network::SocketListener *>& listeners, bool* caughtSignal,
  const std::function<void (int infd, int outfd)>& terminalHandler);

#endif // __TERMINAL_H__
