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

#include <stdlib.h>
#include <stdint.h>

#include "libmpcodecs/mp_image_utils.h"

#include "libmpcodecs/mp_image.h"
#include "libmpcodecs/sws_utils.h"
#include "libvo/csputils.h"

void mp_image_swscale_rows(struct mp_image *dst, int dstRow, int dstRows,
                           int dstRowStep, const struct mp_image *src,
                           int srcRow, int srcRows,
                           int srcRowStep,
                           struct mp_csp_details *csp)
{
    int src_chroma_y_shift = src->chroma_y_shift ==
                             31 ? 0 : src->chroma_y_shift;
    int dst_chroma_y_shift = dst->chroma_y_shift ==
                             31 ? 0 : dst->chroma_y_shift;
    int mask = ((1 << dst_chroma_y_shift) - 1) | ((1 << src_chroma_y_shift) - 1);
    if ((dstRow | dstRows | srcRow | srcRows) & mask)
        mp_msg(
            MSGT_VO, MSGL_ERR,
            "region_to_region: chroma y shift: cannot copy src row %d length %d to dst row %d length %d without problems, the output image may be corrupted (%d, %d, %d)\n",
            srcRow, srcRows, dstRow, dstRows, mask, dst->chroma_y_shift,
            src->chroma_y_shift);
    struct SwsContext *sws =
        sws_getContextFromCmdLine_hq(src->w, srcRows, src->imgfmt, dst->w,
                                     dstRows,
                                     dst->imgfmt);
    struct mp_csp_details mycsp = MP_CSP_DETAILS_DEFAULTS;
    if (csp)
        mycsp = *csp;
    mp_sws_set_colorspace(sws, &mycsp);
    const uint8_t *const src_planes[4] = {
        src->planes[0] + srcRow * src->stride[0],
        src->planes[1] + (srcRow >> src_chroma_y_shift) * src->stride[1],
        src->planes[2] + (srcRow >> src_chroma_y_shift) * src->stride[2],
        src->planes[3] + (srcRow >> src_chroma_y_shift) * src->stride[3]
    };
    uint8_t *const dst_planes[4] = {
        dst->planes[0] + dstRow * dst->stride[0],
        dst->planes[1] + (dstRow >> dst_chroma_y_shift) * dst->stride[1],
        dst->planes[2] + (dstRow >> dst_chroma_y_shift) * dst->stride[2],
        dst->planes[3] + (dstRow >> dst_chroma_y_shift) * dst->stride[3]
    };
    const int src_stride[4] = {
        src->stride[0] * srcRowStep,
        src->stride[1] * srcRowStep,
        src->stride[2] * srcRowStep,
        src->stride[3] * srcRowStep
    };
    const int dst_stride[4] = {
        dst->stride[0] * dstRowStep,
        dst->stride[1] * dstRowStep,
        dst->stride[2] * dstRowStep,
        dst->stride[3] * dstRowStep
    };
    sws_scale(sws, src_planes, src_stride, 0, srcRows, dst_planes, dst_stride);
    sws_freeContext(sws);
}

static void mp_image_blend_plane_const16_with_alpha(uint8_t *dst,
                                                    ssize_t dstRowStride,
                                                    uint8_t srcp,
                                                    const uint8_t *srca,
                                                    ssize_t srcaRowStride,
                                                    uint8_t srcamul, int rows,
                                                    int cols)
{
    int i, j;
    for (i = 0; i < rows; ++i) {
        uint16_t *dstr = (uint16_t *) (dst + dstRowStride * i);
        const uint8_t *srcar = srca + srcaRowStride * i;
        for (j = 0; j < cols; ++j) {
            uint16_t dstp = dstr[j];
            uint32_t srcap = srcar[j]; // 32bit to force the math ops to operate on 32 bit
            srcap *= srcamul; // now 0..65025
            uint16_t outp =
                (srcp * srcap * srcamul + dstp *
                 (65025 - srcap) + 32512) / 65025;
            dstr[j] = outp;
        }
    }
}

static void mp_image_blend_plane_src16_with_alpha(uint8_t *dst,
                                                  ssize_t dstRowStride,
                                                  const uint8_t *src,
                                                  ssize_t srcRowStride,
                                                  const uint8_t *srca,
                                                  ssize_t srcaRowStride,
                                                  uint8_t srcamul, int rows,
                                                  int cols)
{
    int i, j;
    for (i = 0; i < rows; ++i) {
        uint16_t *dstr = (uint16_t *) (dst + dstRowStride * i);
        const uint16_t *srcr = (const uint16_t *) (src + srcRowStride * i);
        const uint8_t *srcar = srca + srcaRowStride * i;
        for (j = 0; j < cols; ++j) {
            uint16_t dstp = dstr[j];
            uint16_t srcp = srcr[j];
            uint32_t srcap = srcar[j]; // 32bit to force the math ops to operate on 32 bit
            srcap *= srcamul; // now 0..65025
            uint16_t outp =
                (srcp * srcamul +
                 127) / 255 + (dstp * (65025 - srcap) + 32512) / 65025;                              // premultiplied alpha GL_ONE GL_ONE_MINUS_SRC_ALPHA
            dstr[j] = outp;
        }
    }
}

static void mp_image_blend_plane_const8_with_alpha(uint8_t *dst,
                                                   ssize_t dstRowStride,
                                                   uint8_t srcp,
                                                   const uint8_t *srca,
                                                   ssize_t srcaRowStride,
                                                   uint8_t srcamul, int rows,
                                                   int cols)
{
    int i, j;
    for (i = 0; i < rows; ++i) {
        uint8_t *dstr = dst + dstRowStride * i;
        const uint8_t *srcar = srca + srcaRowStride * i;
        for (j = 0; j < cols; ++j) {
            uint8_t dstp = dstr[j];
            uint32_t srcap = srcar[j]; // 32bit to force the math ops to operate on 32 bit
            srcap *= srcamul; // now 0..65025
            uint8_t outp =
                (srcp * srcap + dstp * (65025 - srcap) + 32512) / 65025;
            dstr[j] = outp;
        }
    }
}

static void mp_image_blend_plane_src8_with_alpha(uint8_t *dst,
                                                 ssize_t dstRowStride,
                                                 const uint8_t *src,
                                                 ssize_t srcRowStride,
                                                 const uint8_t *srca,
                                                 ssize_t srcaRowStride,
                                                 uint8_t srcamul, int rows,
                                                 int cols)
{
    int i, j;
    for (i = 0; i < rows; ++i) {
        uint8_t *dstr = dst + dstRowStride * i;
        const uint8_t *srcr = src + srcRowStride * i;
        const uint8_t *srcar = srca + srcaRowStride * i;
        for (j = 0; j < cols; ++j) {
            uint8_t dstp = dstr[j];
            uint8_t srcp = srcr[j];
            uint32_t srcap = srcar[j]; // 32bit to force the math ops to operate on 32 bit
            srcap *= srcamul; // now 0..65025
            uint8_t outp =
                (srcp * srcamul +
                 127) / 255 + (dstp * (65025 - srcap) + 32512) / 65025;                             // premultiplied alpha GL_ONE GL_ONE_MINUS_SRC_ALPHA
            dstr[j] = outp;
        }
    }
}

void mp_image_blend_plane_with_alpha(uint8_t *dst, ssize_t dstRowStride,
                                     const uint8_t *src, ssize_t srcRowStride,
                                     uint8_t srcp, const uint8_t *srca,
                                     ssize_t srcaRowStride, uint8_t srcamul,
                                     int rows, int cols,
                                     int bytes)
{
    if (bytes == 2) {
        if (src)
            mp_image_blend_plane_src16_with_alpha(dst, dstRowStride, src,
                                                  srcRowStride, srca,
                                                  srcaRowStride, srcamul, rows,
                                                  cols);
        else
            mp_image_blend_plane_const16_with_alpha(dst, dstRowStride, srcp,
                                                    srca, srcaRowStride,
                                                    srcamul, rows,
                                                    cols);
    } else if (bytes == 1) {
        if (src)
            mp_image_blend_plane_src8_with_alpha(dst, dstRowStride, src,
                                                 srcRowStride, srca,
                                                 srcaRowStride, srcamul, rows,
                                                 cols);
        else
            mp_image_blend_plane_const8_with_alpha(dst, dstRowStride, srcp,
                                                   srca, srcaRowStride, srcamul,
                                                   rows,
                                                   cols);
    }
}
