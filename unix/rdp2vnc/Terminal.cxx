/* Copyright Dinglan Peng
 * Copyright 2017 Peter Astrand <astrand@cendio.se> for Cendio AB
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

#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <iostream>
#include <clocale>
#include <cassert>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <rfb/Logger_stdio.h>
#include <rfb/LogWriter.h>
#include <rfb/VNCServerST.h>
#include <rfb/Configuration.h>
#include <rfb/Timer.h>
#include <network/TcpSocket.h>
#include <network/UnixSocket.h>

#include <xkbcommon/xkbcommon.h>
#include <vterm.h>
#include <utf8cpp/utf8.h>

#include <rdp2vnc/Terminal.h>
#include <rdp2vnc/Geometry.h>
#include <rdp2vnc/font.h>
#include <rdp2vnc/key.h>

static const int glyphWidth = 16;
static const int glyphHeight = 16;

using namespace std;
using namespace rfb;
using namespace network;

static rfb::LogWriter vlog("Terminal");

TerminalDesktop::TerminalDesktop(Geometry* geometry_)
  : geometry(geometry_), pb(NULL), server(NULL), running(false), vt(NULL), requestedWidth(-1), requestedHeight(-1),
    defaultFGColor(0x00f8f8f2), defaultBGColor(0x00272822)
{
  PixelFormat format(32, 24, false, true, 255, 255, 255, 16, 8, 0);
  pb = new ManagedPixelBuffer(format, geometry->width(), geometry->height());
  int stride;
  uint32_t* buffer = (uint32_t *)pb->getBufferRW(geometry->getRect(), &stride);
  int width = geometry->width();
  int height = geometry->height();
  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; ++j) {
      buffer[i * stride + j] = defaultBGColor;
    }
  }
}

TerminalDesktop::~TerminalDesktop() {
  if (vt) {
    vterm_free(vt);
  }
  close(inPipeFd[0]);
  close(inPipeFd[1]);
  close(outPipeFd[0]);
  close(outPipeFd[1]);
}

bool TerminalDesktop::initTerminal(int lines, int cols) {
  if (lines == -1 || lines * glyphHeight > geometry->height()) {
    lines = geometry->height() / glyphHeight;
  }
  if (cols == -1 || cols * glyphWidth / 2 > geometry->width()) {
    cols = geometry->width() / (glyphWidth / 2);
  }
  this->lines = lines;
  this->cols = cols;
  vt = vterm_new(lines, cols);
  if (!vt) {
    return false;
  }
  vterm_set_utf8(vt, 1);
  vtScreen = vterm_obtain_screen(vt);
  vtState = vterm_obtain_state(vt);
  vterm_screen_reset(vtScreen, 1);
  vterm_state_reset(vtState, 1);
  memset(&screenCallbacks, 0, sizeof(VTermScreenCallbacks));
  screenCallbacks.damage = screenDamage;
  screenCallbacks.movecursor = screenMoveCursor;
  vterm_screen_set_callbacks(vtScreen, &screenCallbacks, this);
  vterm_output_set_callback(vt, terminalOutputCallback, this);
  VTermColor fg, bg;
  vterm_color_rgb(&fg, (defaultFGColor & 0xff000000) >> 24,
    (defaultFGColor & 0xff0000) >> 16, (defaultFGColor & 0xff00) >> 8);
  vterm_color_rgb(&bg, (defaultBGColor & 0xff000000) >> 24,
    (defaultBGColor & 0xff0000) >> 16, (defaultBGColor & 0xff00) >> 8);
  vterm_state_set_default_colors(vtState, &fg, &bg);
  if (pipe(inPipeFd) < 0 || pipe(outPipeFd) < 0) {
    return false;
  }
  return true;
}

void TerminalDesktop::start(VNCServer* vs) {
  server = vs;
  server->setPixelBuffer(pb, computeScreenLayout());
  running = true;
}

void TerminalDesktop::stop() {
  running = false;

  //server->setPixelBuffer(0);
}

void TerminalDesktop::terminate() {
}

bool TerminalDesktop::isRunning() {
  return running;
}

void TerminalDesktop::queryConnection(network::Socket* sock,
                                      const char* userName)
{
  assert(isRunning());
}

void TerminalDesktop::pointerEvent(const Point& pos, int buttonMask) {
  return;
  int mod = VTERM_MOD_NONE;
  if (pressedKeys.count(XKB_KEY_Shift_L) || pressedKeys.count(XKB_KEY_Shift_R)) {
    mod |= VTERM_MOD_SHIFT;
  }
  if (pressedKeys.count(XKB_KEY_Alt_L) || pressedKeys.count(XKB_KEY_Alt_R)) {
    mod |= VTERM_MOD_ALT;
  }
  if (pressedKeys.count(XKB_KEY_Control_L) || pressedKeys.count(XKB_KEY_Control_R)) {
    mod |= VTERM_MOD_CTRL;
  }
  int padx = (geometry->width() - cols * glyphWidth / 2) / 2;
  int pady = (geometry->height() - lines * glyphHeight) / 2;
  int x = (pos.x - padx) / (glyphWidth / 2);
  int y = (pos.y - pady) / glyphHeight;
  vterm_mouse_move(vt, y, x, (VTermModifier)mod);
  vterm_mouse_button(vt, 1, buttonMask & 1, (VTermModifier)mod);
  vterm_mouse_button(vt, 2, buttonMask & 2, (VTermModifier)mod);
  vterm_mouse_button(vt, 3, buttonMask & 4, (VTermModifier)mod);
}

void TerminalDesktop::keyEvent(rdr::U32 keysym, rdr::U32 xtcode, bool down) {
  if (down) {
    pressedKeys.insert(keysym);
  } else {
    pressedKeys.erase(keysym);
  }
  if (down) {
    if (keysym == XKB_KEY_Shift_L || keysym == XKB_KEY_Shift_R ||
        keysym == XKB_KEY_Alt_L || keysym == XKB_KEY_Alt_R ||
        keysym == XKB_KEY_Control_L || keysym == XKB_KEY_Control_R) {
      return;
    }
    int mod = VTERM_MOD_NONE;
    if (pressedKeys.count(XKB_KEY_Shift_L) || pressedKeys.count(XKB_KEY_Shift_R)) {
      mod |= VTERM_MOD_SHIFT;
    }
    if (pressedKeys.count(XKB_KEY_Alt_L) || pressedKeys.count(XKB_KEY_Alt_R)) {
      mod |= VTERM_MOD_ALT;
    }
    if (pressedKeys.count(XKB_KEY_Control_L) || pressedKeys.count(XKB_KEY_Control_R)) {
      mod |= VTERM_MOD_CTRL;
    }
    if ((mod & VTERM_MOD_CTRL) && (keysym == 'v' || keysym == 'V')) {
      // Ctrl-V
      try {
        server->requestClipboard();
      } catch(rdr::Exception& e) {
        vlog.error("Request clipboard: %s", e.str());
      }
      return;
    }
    auto it = keysymVTermKeyMap.find(keysym);
    if (it != keysymVTermKeyMap.end()) {
      vterm_keyboard_key(vt, (VTermKey)it->second, (VTermModifier)mod);
    } else {
      uint32_t ch = xkb_keysym_to_utf32(keysym);
      vterm_keyboard_unichar(vt, ch, (VTermModifier)mod);
    }
  }
}

void TerminalDesktop::clientCutText(const char* str) {
}

ScreenSet TerminalDesktop::computeScreenLayout()
{
  ScreenSet layout;

  // Make sure that we have at least one screen
  if (layout.num_screens() == 0)
    layout.add_screen(rfb::Screen(0, 0, 0, geometry->width(),
                                  geometry->height(), 0));

  return layout;
}

unsigned int TerminalDesktop::setScreenLayout(int fb_width, int fb_height,
                                         const rfb::ScreenSet& layout)
{
  if (layout.num_screens() == 1) {
    requestedWidth = fb_width;
    requestedHeight = fb_height;
  }
  return rfb::resultProhibited;
}

void TerminalDesktop::handleClipboardRequest() {
}

void TerminalDesktop::handleClipboardAnnounce(bool available) {
}

void TerminalDesktop::handleClipboardData(const char* data) {
  vterm_keyboard_start_paste(vt);
  terminalOutput(data, strlen(data));
  vterm_keyboard_end_paste(vt);
}

rfb::Rect TerminalDesktop::terminalPosToRFBRect(int x, int y, int width) {
  int padx = (geometry->width() - cols * glyphWidth / 2) / 2;
  int pady = (geometry->height() - lines * glyphHeight) / 2;
  int x1 = padx + glyphWidth / 2 * x;
  int y1 = pady + glyphHeight * y;
  int x2 = x1 + glyphWidth * width / 2;
  int y2 = y1 + glyphHeight;
  return Rect(x1, y1, x2, y2);
}

rfb::Rect TerminalDesktop::terminalLineToRFBRect(int line) {
  int padx = (geometry->width() - cols * glyphWidth / 2) / 2;
  int pady = (geometry->height() - lines * glyphHeight) / 2;
  int x1 = padx;
  int y1 = pady + glyphHeight * line;
  int x2 = geometry->width() - padx;
  int y2 = y1 + glyphHeight;
  return Rect(x1, y1, x2, y2);
}

rfb::Rect TerminalDesktop::terminalRectToRFBRect(int x1, int x2, int y1, int y2) {
  int padx = (geometry->width() - cols * glyphWidth / 2) / 2;
  int pady = (geometry->height() - lines * glyphHeight) / 2;
  int x1r = padx + glyphWidth / 2 * x1;
  int y1r = pady + glyphHeight * y1;
  int x2r = padx + glyphWidth / 2 * x2;
  int y2r = pady + glyphHeight * y2;
  return Rect(x1r, y1r, x2r, y2r);
}

int TerminalDesktop::getInFd() {
  return inPipeFd[0];
}

int* TerminalDesktop::getOutFds() {
  return outPipeFd;
}

VTerm* TerminalDesktop::getTerminal() {
  return vt;
}

std::pair<int, int> TerminalDesktop::getRequestedDesktopSize() {
  return make_pair(requestedWidth, requestedHeight);
}

const Geometry& TerminalDesktop::getGeometry() {
  return *geometry;
}

void TerminalDesktop::terminalOutputCallback(const char* s, size_t len, void* user) {
  TerminalDesktop* desktop = (TerminalDesktop *)user;
  desktop->terminalOutput(s, len);
}

int TerminalDesktop::screenDamage(VTermRect rect, void* user) {
  TerminalDesktop* desktop = (TerminalDesktop *)user;
  desktop->damage(rect);
  return 1;
}

int TerminalDesktop::screenMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user) {
  TerminalDesktop* desktop = (TerminalDesktop *)user;
  desktop->moveCursor(pos, oldpos, visible);
  return 1;
}

void TerminalDesktop::damage(VTermRect rect) {
  int x1 = rect.start_col;
  int x2 = rect.end_col;
  int y1 = rect.start_row;
  int y2 = rect.end_row;
  if (x1 >= x2 || y1 >= y2) {
    return;
  }
  if (x1 < 0 || x2 > cols || y1 < 0 || y2 > lines) {
    return;
  }
  for (int i = y1; i < y2; ++i) {
    for (int j = x1; j < x2; ++j) {
      VTermPos pos {i, j};
      renderCell(pos);
    }
  }
}

void TerminalDesktop::moveCursor(VTermPos pos, VTermPos oldpos, bool visible) {
  if (pos.row < 0 || pos.row >= lines || pos.col < 0 || pos.col >= cols) {
    return;
  }
  if (visible) {
    renderCursor(pos);
  }
  if (oldpos.row < 0 || oldpos.row >= lines || oldpos.col < 0 || oldpos.col >= cols) {
    return;
  }
  renderCell(oldpos);
}

void TerminalDesktop::renderCursor(VTermPos pos) {
  VTermScreenCell cell;
  if (!vterm_screen_get_cell(vtScreen, pos, &cell)) {
    return;
  }
  int x = pos.col;
  int y = pos.row;
  uint32_t ch = cell.chars[0];
  if (ch == (uint32_t)(-1)) {
    renderGlyph(x, y, 1, 0, defaultBGColor, defaultFGColor);
  } else {
    renderCell(pos, true);
  }
}

void TerminalDesktop::renderCell(VTermPos pos, bool reverse) {
  VTermScreenCell cell;
  if (!vterm_screen_get_cell(vtScreen, pos, &cell)) {
    return;
  }
  uint32_t ch = cell.chars[0];
  if (ch == (uint32_t)(-1)) {
    return;
  }
  int x = pos.col;
  int y = pos.row;
  if (x < 0 || x >= cols || y < 0 || y >= lines) {
    return;
  }
  int width = cell.width;
  uint32_t fg = defaultFGColor;
  uint32_t bg = defaultBGColor;
  VTermColor fgc = cell.fg;
  VTermColor bgc = cell.bg;
  if (!VTERM_COLOR_IS_DEFAULT_FG(&fgc)) {
    vterm_state_convert_color_to_rgb(vtState, &fgc);
    fg = (fgc.rgb.red << 16) | (fgc.rgb.green << 8) | fgc.rgb.blue;
  }
  if (!VTERM_COLOR_IS_DEFAULT_BG(&bgc)) {
    vterm_state_convert_color_to_rgb(vtState, &bgc);
    bg = (bgc.rgb.red << 16) | (bgc.rgb.green << 8) | fgc.rgb.blue;
  }
  if (reverse) {
    swap(fg, bg);
  }
  renderGlyph(x, y, width, ch, fg, bg);
}

void TerminalDesktop::terminalOutput(const char* s, size_t len) {
  if (inPipeFd[1] != -1) {
    write(inPipeFd[1], s, len);
  }
}

void TerminalDesktop::renderGlyph(int x, int y, int width, uint32_t ch, uint32_t fg, uint32_t bg) {
  if (width > 2) {
    width = 2;
  }
  if (x == cols - 1) {
    width = 1;
  }
  Rect glyphRect = terminalPosToRFBRect(x, y, width);
  int stride;
  uint32_t* buffer = (uint32_t *)pb->getBufferRW(glyphRect, &stride);
  size_t glyphIndex = ch < glyphBitmapSize ? ch : 0;
  if (ch == 0) {
    for (int i = 0; i < glyphHeight; ++i) {
      int gw = width == 2 ? glyphWidth : glyphWidth / 2;
      for (int j = 0; j < gw; ++j) {
        buffer[i * stride + j] = bg;
      }
    }
  } else {
    for (int i = 0; i < glyphHeight; ++i) {
      uint16_t line = glyphBitmap[glyphIndex][i];
      int gw = width == 2 ? glyphWidth : glyphWidth / 2;
      for (int j = 0; j < gw; ++j) {
        if (line & (1 << j)) {
          buffer[i * stride + j] = fg;
        } else {
          buffer[i * stride + j] = bg;
        }
      }
    }
  }
  if (server) {
    try {
      server->add_changed(Region(glyphRect));
    } catch (rfb::Exception& e) {
      vlog.error("Add change: %s", e.str());
    }
  }
}

bool runTerminal(TerminalDesktop* desktop, rfb::VNCServerST* server,
  std::list<SocketListener *>& listeners, bool* caughtSignal,
  const std::function<void (int infd, int outfd)>& terminalHandler) {

  int inFd = desktop->getInFd();
  int* outFds = desktop->getOutFds();
  fcntl(outFds[0], F_SETFL, fcntl(outFds[0], F_GETFL, 0) | O_NONBLOCK);
  char buffer[2048];
  atomic_bool hasTerminalFinished = false;

  thread handlerThread([&] {
    terminalHandler(inFd, outFds[1]);
    hasTerminalFinished = true;
  });
  handlerThread.detach();

  while (true) {
    if (*caughtSignal) {
      return false;
    }
    struct timeval tv;
    fd_set rfds, wfds;
    std::list<Socket*> sockets;
    std::list<Socket*>::iterator i;
        
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);

    for (std::list<SocketListener*>::iterator i = listeners.begin();
         i != listeners.end();
         i++)
      FD_SET((*i)->getFd(), &rfds);
    FD_SET(outFds[0], &rfds);

    server->getSockets(&sockets);
    int clients_connected = 0;
    for (i = sockets.begin(); i != sockets.end(); i++) {
      if ((*i)->isShutdown()) {
        server->removeSocket(*i);
        delete (*i);
      } else {
        FD_SET((*i)->getFd(), &rfds);
        if ((*i)->outStream().hasBufferedData())
          FD_SET((*i)->getFd(), &wfds);
        clients_connected++;
      }
    }

    tv.tv_sec = 0;
    tv.tv_usec = 10000;
    // Do the wait...
    int n = select(FD_SETSIZE, &rfds, &wfds, 0, &tv);

    if (n < 0) {
      if (errno == EINTR) {
        vlog.debug("Interrupted select() system call");
        continue;
      } else {
        throw rdr::SystemException("select", errno);
      }
    }

    if (FD_ISSET(outFds[0], &rfds)) {
      ssize_t len = read(outFds[0], buffer, sizeof(buffer));
      if (len > 0) {
        VTerm* vt = desktop->getTerminal();
        if (vt) {
          vterm_input_write(vt, buffer, len);
        }
      }
    }


    for (std::list<SocketListener*>::iterator i = listeners.begin();
         i != listeners.end();
         i++) {
      if (FD_ISSET((*i)->getFd(), &rfds)) {
        Socket* sock = (*i)->accept();
        if (sock) {
          server->addSocket(sock);
        } else {
          vlog.status("Client connection rejected");
        }
      }
    }

    Timer::checkTimeouts();

    // Client list could have been changed.
    server->getSockets(&sockets);

    // Nothing more to do if there are no client connections.
    if (sockets.empty())
      continue;

    // Process events on existing VNC connections
    for (i = sockets.begin(); i != sockets.end(); i++) {
      if (FD_ISSET((*i)->getFd(), &rfds)) {
        server->processSocketReadEvent(*i);
      }
      if (FD_ISSET((*i)->getFd(), &wfds)) {
        server->processSocketWriteEvent(*i);
      }
    }
    if (hasTerminalFinished) {
      return true;
    }
  }
}