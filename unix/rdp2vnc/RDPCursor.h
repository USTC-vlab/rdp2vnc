/* Copyright (C) 2021 DInglan Peng
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

#ifndef __RDPCURSOR_H__
#define __RDPCURSOR_H__

#include <inttypes.h>

struct RDPCursor {
  RDPCursor(const uint8_t* data_, size_t size_, int width_, int height_, int x_, int y_)
    : data(new uint8_t[size_]), size(size_), width(width_), height(height_), x(x_), y(y_), posX(-1), posY(-1)
  {
    if (data_ && data) {
      memcpy(data, data_, size);
    }
  }
  ~RDPCursor() {
    if (data) {
      delete[] data;
    }
  }
  uint8_t* data;
  size_t size;
  int width;
  int height;
  int x;
  int y;
  int posX;
  int posY;
};

#endif // __RDPCURSOR_H__
