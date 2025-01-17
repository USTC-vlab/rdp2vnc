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

#include <iostream>
#include <memory>
#include <assert.h>
#include <signal.h>
#include <unistd.h>

#include <rfb/LogWriter.h>

#include <rdp2vnc/RDPDesktop.h>
#include <rdp2vnc/RDPClient.h>
#include <rdp2vnc/Geometry.h>
#include <rdp2vnc/RDPPixelBuffer.h>
#include <rdp2vnc/RDPCursor.h>

using namespace std;
using namespace rfb;

static rfb::LogWriter vlog("RDPDesktop");

// order is important as it must match RFB extension
//static const char * ledNames[RDPDESKTOP_N_LEDS] = {
//  "Scroll Lock", "Num Lock", "Caps Lock"
//};

RDPDesktop::RDPDesktop(Geometry* geometry_, RDPClient* client_)
  : geometry(geometry_), server(NULL), client(client_), running(false)
{
}

RDPDesktop::~RDPDesktop() {
}

void RDPDesktop::start(VNCServer* vs) {

  pb.reset(new RDPPixelBuffer(geometry->getRect(), client));

  server = vs;
  server->setPixelBuffer(pb.get(), computeScreenLayout());

  //server->setLEDState(ledState);

  if (firstCursor) {
    try {
      server->setCursor(firstCursor->width, firstCursor->height,
        Point(firstCursor->x, firstCursor->y), firstCursor->data);
    } catch (rdr::Exception& e) {
      vlog.error("Set cursor: %s", e.str());
    }
    if (firstCursor->posX >= 0 && firstCursor->posY >= 0) {
      try {
        server->setCursorPos(Point(firstCursor->posX, firstCursor->posY), false);
      } catch (rdr::Exception& e) {
        vlog.error("Set cursor position: %s", e.str());
      }
    }
    firstCursor.reset();
  }

  running = true;
}

void RDPDesktop::stop() {
  running = false;

  server->setPixelBuffer(0);
  server = NULL;
}

void RDPDesktop::terminate() {
  kill(getpid(), SIGTERM);
}

bool RDPDesktop::isRunning() {
  return running;
}

void RDPDesktop::queryConnection(network::Socket* sock,
                               const char* userName)
{
  assert(isRunning());
}

void RDPDesktop::pointerEvent(const Point& pos, int buttonMask) {
  client->pointerEvent(pos, buttonMask);
}

void RDPDesktop::keyEvent(rdr::U32 keysym, rdr::U32 xtcode, bool down) {
  client->keyEvent(keysym, xtcode, down);
}

void RDPDesktop::clientCutText(const char* str) {
}

ScreenSet RDPDesktop::computeScreenLayout()
{
  ScreenSet layout;

  // Make sure that we have at least one screen
  if (layout.num_screens() == 0)
    layout.add_screen(rfb::Screen(0, 0, 0, geometry->width(),
                                  geometry->height(), 0));

  return layout;
}

unsigned int RDPDesktop::setScreenLayout(int fb_width, int fb_height,
                                         const rfb::ScreenSet& layout)
{
  return client->setScreenLayout(fb_width, fb_height, layout);
}

void RDPDesktop::handleClipboardRequest() {
  client->handleClipboardRequest();
}

void RDPDesktop::handleClipboardAnnounce(bool available) {
  client->handleClipboardAnnounce(available);
}

void RDPDesktop::handleClipboardData(const char* data) {
  client->handleClipboardData(data);
}

bool RDPDesktop::setFirstCursor(std::shared_ptr<RDPCursor> &cursor)
{
  firstCursor = cursor;
  return true;
}

bool RDPDesktop::resize() {
  int width = client->width();
  int height = client->height();
  geometry->recalc(width, height);
  if (server) {
    pb.reset(new RDPPixelBuffer(geometry->getRect(), client));
    server->setPixelBuffer(pb.get(), computeScreenLayout());
  }
  return true;
}