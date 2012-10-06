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

#ifndef MPLAYER_MP_IMAGE_UTILS_H
#define MPLAYER_MP_IMAGE_UTILS_H

struct mp_image;
struct mp_csp_details;

// sws stuff
void mp_image_swscale_rows(struct mp_image *dst, int dstRow, int dstRows,
                           int dstRowStep, const struct mp_image *src,
                           int srcRow, int srcRows,
                           int srcRowStep,
                           struct mp_csp_details *csp);

// alpha blending (this works on single planes!)
void mp_image_blend_plane_src_with_alpha(uint8_t *dst, ssize_t dstRowStride,
                                         const uint8_t *src, ssize_t srcRowStride,
                                         const uint8_t *srca, ssize_t srcaRowStride,
                                         uint8_t srcamul,
                                         int rows, int cols, int bytes);
void mp_image_blend_plane_const_with_alpha(uint8_t *dst, ssize_t dstRowStride,
                                           uint8_t srcp,
                                           const uint8_t *srca, ssize_t srcaRowStride,
                                           uint8_t srcamul,
                                           int rows, int cols, int bytes);

#endif /* MPLAYER_MP_IMAGE_UTILS_H */
