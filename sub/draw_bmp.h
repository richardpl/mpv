/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef MPLAYER_DRAW_BMP_H
#define MPLAYER_DRAW_BMP_H

struct mp_image;
struct sub_bitmaps;
struct mp_csp_details;
void mp_draw_sub_bitmaps(struct mp_image *dst, struct sub_bitmaps *sbs,
                         struct mp_csp_details *csp);

#endif /* MPLAYER_DRAW_BMP_H */

// vim: ts=4 sw=4 et tw=80
