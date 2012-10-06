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

#ifndef MPLAYER_SWS_UTILS_H
#define MPLAYER_SWS_UTILS_H

#include <libswscale/swscale.h>

struct mp_image;
struct mp_csp_details;

void sws_getFlagsAndFilterFromCmdLine(int *flags, SwsFilter **srcFilterParam,
                                      SwsFilter **dstFilterParam);
struct SwsContext *sws_getContextFromCmdLine(int srcW, int srcH, int srcFormat,
                                             int dstW, int dstH,
                                             int dstFormat);
struct SwsContext *sws_getContextFromCmdLine_hq(int srcW, int srcH,
                                                int srcFormat, int dstW,
                                                int dstH,
                                                int dstFormat);
int mp_sws_set_colorspace(struct SwsContext *sws, struct mp_csp_details *csp);

// sws stuff
void mp_image_get_supported_regionstep(int *sx, int *sy,
                                       const struct mp_image *img);
void mp_image_swscale(struct mp_image *dst,
                      const struct mp_image *src,
                      struct mp_csp_details *csp);
void mp_image_swscale_region(struct mp_image *dst,
                             int dx, int dy, int dw, int dh, int dstRowStep,
                             const struct mp_image *src,
                             int sx, int sy, int sw, int sh, int srcRowStep,
                             struct mp_csp_details *csp);

// alpha blending (this works on single planes!)
// note: src is assumed to be premultiplied
void mp_blend_src_alpha(uint8_t *dst, ssize_t dstRowStride,
                        const uint8_t *src, ssize_t srcRowStride,
                        const uint8_t *srca, ssize_t srcaRowStride,
                        uint8_t srcamul,
                        int rows, int cols, int bytes);
void mp_blend_const_alpha(uint8_t *dst, ssize_t dstRowStride,
                          uint8_t srcp,
                          const uint8_t *srca, ssize_t srcaRowStride,
                          uint8_t srcamul,
                          int rows, int cols, int bytes);

#endif /* MP_SWS_UTILS_H */
