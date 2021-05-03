/* Copyright (C) 2007-2008 Constantin Kaplinsky.  All Rights Reserved.
 * Copyright 2014 Pierre Ossman for Cendio AB
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
// RDPPixelBuffer.cxx
//

#include <vector>
#include <rfb/Region.h>
#include <rdp2vnc/RDPPixelBuffer.h>
#include <rdp2vnc/RDPClient.h>

using namespace rfb;

RDPPixelBuffer::RDPPixelBuffer(const Rect &rect, RDPClient* client_)
  : FullFramePixelBuffer(), client(client_)
{
  format = PixelFormat(32, 24, false, true, 255, 255, 255, 16, 8, 0);
  setBuffer(client->width(), client->height(), client->getBuffer(), client->width());
}

RDPPixelBuffer::~RDPPixelBuffer()
{
}