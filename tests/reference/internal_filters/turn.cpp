// Avisynth v2.5.  Copyright 2002 Ben Rudiak-Gould et al.
// http://avisynth.nl

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA, or visit
// http://www.gnu.org/copyleft/gpl.html .
//
// Linking Avisynth statically or dynamically with other modules is making a
// combined work based on Avisynth.  Thus, the terms and conditions of the GNU
// General Public License cover the whole combination.
//
// As a special exception, the copyright holders of Avisynth give you
// permission to link Avisynth with independent modules that communicate with
// Avisynth solely through the interfaces defined in avisynth.h, regardless of the license
// terms of these independent modules, and to copy and distribute the
// resulting combined work under terms of your choice, provided that
// every copy of the combined work is accompanied by a complete copy of
// the source code of Avisynth (the version of Avisynth used to produce the
// combined work), being distributed under the terms of the GNU General
// Public License plus this exception.  An independent module is a module
// which is not derived from or based on Avisynth, such as 3rd-party filters,
// import and export plugins, or graphical user interfaces.

/*
** Turn. version 0.1
** (c) 2003 - Ernst Peché
**
*/

#include "turn.h"
#ifdef INTEL_INTRINSICS
#include "intel/turn_sse.h"
#endif
#include "core/internal.h"
#include <stdint.h>
#include "convert/convert_helper.h"




enum TurnDirection
{
  DIRECTION_LEFT = 0,
  DIRECTION_RIGHT = 1,
  DIRECTION_180 = 2
};


// TurnLeft() is FlipVertical().TurnRight().FlipVertical().
// Therefore, we don't have to implement both TurnRight() and TurnLeft().

template <typename T>
void turn_right_plane_c(const BYTE* srcp, BYTE* dstp, int src_rowsize, int height, int src_pitch, int dst_pitch)
{
    const BYTE* s0 = srcp + src_pitch * (height - 1);

    for (int y = 0; y < height; ++y)
    {
        BYTE* d0 = dstp;
        for (int x = 0; x < src_rowsize; x += sizeof(T))
        {
            *reinterpret_cast<T*>(d0) = *reinterpret_cast<const T*>(s0 + x);
            d0 += dst_pitch;
        }
        s0 -= src_pitch;
        dstp += sizeof(T);
    }
}


// Explicit instantiation, can be used from other modules.
template void turn_right_plane_c<uint64_t>(const BYTE*, BYTE*, int, int, int, int);


void turn_right_plane_8_c(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    turn_right_plane_c<BYTE>(srcp, dstp, src_rowsize, src_height, src_pitch, dst_pitch);
}


void turn_left_plane_8_c(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    turn_right_plane_c<BYTE>(srcp + src_pitch * (src_height - 1), dstp + dst_pitch * (src_rowsize - 1), src_rowsize, src_height, -src_pitch, -dst_pitch);
}


void turn_right_plane_16_c(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    turn_right_plane_c<uint16_t>(srcp, dstp, src_rowsize, src_height, src_pitch, dst_pitch);
}


void turn_left_plane_16_c(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    turn_right_plane_c<uint16_t>(srcp + src_pitch * (src_height - 1), dstp + dst_pitch * (src_rowsize / 2 - 1), src_rowsize, src_height, -src_pitch, -dst_pitch);
}


void turn_right_plane_32_c(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    turn_right_plane_c<uint32_t>(srcp, dstp, src_rowsize, src_height, src_pitch, dst_pitch);
}


void turn_left_plane_32_c(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    turn_right_plane_c<uint32_t>(srcp + src_pitch * (src_height - 1), dstp + dst_pitch * (src_rowsize / 4 - 1), src_rowsize, src_height, -src_pitch, -dst_pitch);
}


// on RGB, TurnLeft and TurnRight are reversed.
void turn_left_rgb32_c(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    turn_right_plane_32_c(srcp, dstp, src_rowsize, src_height, src_pitch, dst_pitch);
}


void turn_right_rgb32_c(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    turn_left_plane_32_c(srcp, dstp, src_rowsize, src_height, src_pitch, dst_pitch);
}


struct Rgb24 {
    BYTE b, g, r;
};


void turn_left_rgb24(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    turn_right_plane_c<Rgb24>(srcp, dstp, src_rowsize, src_height, src_pitch, dst_pitch);
}


void turn_right_rgb24(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    turn_right_plane_c<Rgb24>(srcp + src_pitch * (src_height - 1), dstp + dst_pitch * (src_rowsize / 3 - 1), src_rowsize, src_height, -src_pitch, -dst_pitch);
}


struct Rgb48 {
    uint16_t b, g, r;
};


void turn_left_rgb48_c(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    turn_right_plane_c<Rgb48>(srcp, dstp, src_rowsize, src_height, src_pitch, dst_pitch);
}


void turn_right_rgb48_c(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    turn_right_plane_c<Rgb48>(srcp + src_pitch * (src_height - 1), dstp + dst_pitch * (src_rowsize / 6 - 1), src_rowsize, src_height, -src_pitch, -dst_pitch);
}


void turn_left_rgb64_c(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    turn_right_plane_c<uint64_t>(srcp, dstp, src_rowsize, src_height, src_pitch, dst_pitch);
}


void turn_right_rgb64_c(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    turn_right_plane_c<uint64_t>(srcp + src_pitch * (src_height - 1), dstp + dst_pitch * (src_rowsize / 8 - 1), src_rowsize, src_height, -src_pitch, -dst_pitch);
}


static void turn_right_yuy2(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    dstp += (src_height - 2) * 2;

    for (int y = 0; y < src_height; y += 2)
    {
        BYTE* d0 = dstp - y * 2;
        for (int x = 0; x < src_rowsize; x += 4)
        {
            int u = (srcp[x + 1] + srcp[x + 1 + src_pitch] + 1) / 2;
            int v = (srcp[x + 3] + srcp[x + 3 + src_pitch] + 1) / 2;

            d0[0] = srcp[x + src_pitch];
            d0[1] = u;
            d0[2] = srcp[x];
            d0[3] = v;
            d0 += dst_pitch;

            d0[0] = srcp[x + src_pitch + 2];
            d0[1] = u;
            d0[2] = srcp[x + 2];
            d0[3] = v;
            d0 += dst_pitch;
        }
        srcp += src_pitch * 2;
    }
}


static void turn_left_yuy2(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    turn_right_yuy2(srcp + src_pitch * (src_height - 1), dstp + dst_pitch * (src_rowsize / 2 - 1), src_rowsize, src_height, -src_pitch, -dst_pitch);
}


template <typename T>
void turn_180_plane_c(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    dstp += dst_pitch * (src_height - 1) + src_rowsize - sizeof(T);
    src_rowsize /= sizeof(T);

    for (int y = 0; y < src_height; ++y)
    {
        const T* s0 = reinterpret_cast<const T*>(srcp);
        T* d0 = reinterpret_cast<T*>(dstp);

        for (int x = 0; x < src_rowsize; ++x)
        {
            d0[-x] = s0[x];
        }
        srcp += src_pitch;
        dstp -= dst_pitch;
    }
}

static void turn_180_yuy2(const BYTE* srcp, BYTE* dstp, int src_rowsize, int src_height, int src_pitch, int dst_pitch)
{
    dstp += dst_pitch * (src_height - 1) + src_rowsize - 4;

    for (int y = 0; y < src_height; ++y)
    {
        for (int x = 0; x < src_rowsize; x += 4)
        {
            dstp[-x + 2] = srcp[x + 0];
            dstp[-x + 1] = srcp[x + 1];
            dstp[-x + 0] = srcp[x + 2];
            dstp[-x + 3] = srcp[x + 3];
        }
        srcp += src_pitch;
        dstp -= dst_pitch;
    }
}



template void turn_180_plane_c<uint8_t>(const BYTE*, BYTE*, int, int, int, int);
template void turn_180_plane_c<uint16_t>(const BYTE*, BYTE*, int, int, int, int);
template void turn_180_plane_c<uint32_t>(const BYTE*, BYTE*, int, int, int, int);
template void turn_180_plane_c<uint64_t>(const BYTE*, BYTE*, int, int, int, int);
