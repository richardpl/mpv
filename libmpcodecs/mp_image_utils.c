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
#include <assert.h>

#include "libmpcodecs/mp_image_utils.h"

#include "libmpcodecs/mp_image.h"
#include "libmpcodecs/sws_utils.h"
#include "libmpcodecs/img_format.h"
#include "libvo/csputils.h"

#define SWS_MIN_BITS (16*8) // libswscale currently requires 16 bytes alignment
void mp_image_get_supported_regionstep(int *sx, int *sy,
                                       const struct mp_image *img)
{
    int p;

    if (img->chroma_x_shift == 31)
        *sx = 1;
    else
        *sx = (1 << img->chroma_y_shift);

    if (img->chroma_y_shift == 31)
        *sy = 1;
    else
        *sy = (1 << img->chroma_y_shift);

    for (p = 0; p < img->num_planes; ++p) {
        int bits = MP_IMAGE_BITS_PER_PIXEL_ON_PLANE(img, p);
        while (*sx * bits % SWS_MIN_BITS)
            *sx *= 2;
    }
}

void mp_image_swscale(struct mp_image *dst,
                      const struct mp_image *src,
                      struct mp_csp_details *csp)
{
    struct SwsContext *sws =
        sws_getContextFromCmdLine_hq(src->w, src->h, src->imgfmt, dst->w, dst->h,
                                     dst->imgfmt);
    struct mp_csp_details mycsp = MP_CSP_DETAILS_DEFAULTS;
    if (csp)
        mycsp = *csp;
    mp_sws_set_colorspace(sws, &mycsp);
    sws_scale(sws, (const unsigned char *const *) src->planes, src->stride, 0, src->h, dst->planes, dst->stride);
    sws_freeContext(sws);
}

void mp_image_swscale_region(struct mp_image *dst,
                             int dx, int dy, int dw, int dh, int dstRowStep,
                             const struct mp_image *src,
                             int sx, int sy, int sw, int sh, int srcRowStep,
                             struct mp_csp_details *csp)
{
    int sxstep, systep, dxstep, dystep;
    mp_image_get_supported_regionstep(&dxstep, &dystep, dst);
    mp_image_get_supported_regionstep(&sxstep, &systep, src);

    assert((dx % dxstep) == 0);
    assert((dy % dystep) == 0);
    assert((sx % sxstep) == 0);
    assert((sy % systep) == 0);
    assert((dw % dxstep) == 0 || dx + dw == dst->w);
    assert((dh % dystep) == 0 || dy + dh == dst->h);
    assert((sw % sxstep) == 0 || sx + sw == src->w);
    assert((sh % systep) == 0 || sy + sh == src->h);

    struct SwsContext *sws =
        sws_getContextFromCmdLine_hq(sw, sh, src->imgfmt, dw, dh,
                                     dst->imgfmt);
    struct mp_csp_details mycsp = MP_CSP_DETAILS_DEFAULTS;
    if (csp)
        mycsp = *csp;
    mp_sws_set_colorspace(sws, &mycsp);
    const uint8_t *const src_planes[4] = {
        src->planes[0] + sy * src->stride[0] + sx * MP_IMAGE_BITS_PER_PIXEL_ON_PLANE(src, 0) / 8,
        src->planes[1] + (sy >> src->chroma_y_shift) * src->stride[1] + (sx >> src->chroma_x_shift) * MP_IMAGE_BITS_PER_PIXEL_ON_PLANE(src, 1) / 8,
        src->planes[2] + (sy >> src->chroma_y_shift) * src->stride[2] + (sx >> src->chroma_x_shift) * MP_IMAGE_BITS_PER_PIXEL_ON_PLANE(src, 2) / 8,
        src->planes[3] + (sy >> src->chroma_y_shift) * src->stride[3] + (sx >> src->chroma_x_shift) * MP_IMAGE_BITS_PER_PIXEL_ON_PLANE(src, 3) / 8
    };
    uint8_t *const dst_planes[4] = {
        dst->planes[0] + dy * dst->stride[0] + dx * MP_IMAGE_BITS_PER_PIXEL_ON_PLANE(dst, 0) / 8,
        dst->planes[1] + (dy >> dst->chroma_y_shift) * dst->stride[1] + (dx >> dst->chroma_x_shift) * MP_IMAGE_BITS_PER_PIXEL_ON_PLANE(dst, 1) / 8,
        dst->planes[2] + (dy >> dst->chroma_y_shift) * dst->stride[2] + (dx >> dst->chroma_x_shift) * MP_IMAGE_BITS_PER_PIXEL_ON_PLANE(dst, 2) / 8,
        dst->planes[3] + (dy >> dst->chroma_y_shift) * dst->stride[3] + (dx >> dst->chroma_x_shift) * MP_IMAGE_BITS_PER_PIXEL_ON_PLANE(dst, 3) / 8
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
    sws_scale(sws, src_planes, src_stride, 0, sh, dst_planes, dst_stride);
    sws_freeContext(sws);
}

static void mp_blend_const16_alpha(uint8_t *dst,
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

static void mp_blend_src16_alpha(uint8_t *dst,
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

static void mp_blend_const8_alpha(uint8_t *dst,
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

static void mp_blend_src8_alpha(uint8_t *dst,
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

void mp_blend_src_alpha(uint8_t *dst, ssize_t dstRowStride,
                        const uint8_t *src, ssize_t srcRowStride,
                        const uint8_t *srca, ssize_t srcaRowStride,
                        uint8_t srcamul,
                        int rows, int cols, int bytes)
{
    if (bytes == 2) {
        mp_blend_src16_alpha(dst, dstRowStride, src,
                                              srcRowStride, srca,
                                              srcaRowStride, srcamul, rows,
                                              cols);
    } else if (bytes == 1) {
        mp_blend_src8_alpha(dst, dstRowStride, src,
                                             srcRowStride, srca,
                                             srcaRowStride, srcamul, rows,
                                             cols);
    }
}

void mp_blend_const_alpha(uint8_t *dst, ssize_t dstRowStride,
                          uint8_t srcp,
                          const uint8_t *srca, ssize_t srcaRowStride,
                          uint8_t srcamul,
                          int rows, int cols, int bytes)
{
    if (bytes == 2) {
        mp_blend_const16_alpha(dst, dstRowStride, srcp,
                srca, srcaRowStride,
                srcamul, rows,
                cols);
    } else if (bytes == 1) {
        mp_blend_const8_alpha(dst, dstRowStride, srcp,
                                               srca, srcaRowStride, srcamul,
                                               rows,
                                               cols);
    }
}
