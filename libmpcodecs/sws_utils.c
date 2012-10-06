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

#include <assert.h>

#include "libmpcodecs/sws_utils.h"

#include "libmpcodecs/mp_image.h"
#include "libmpcodecs/img_format.h"
#include "fmt-conversion.h"
#include "libvo/csputils.h"
#include "mp_msg.h"

//global sws_flags from the command line
int sws_flags = 2;

float sws_lum_gblur = 0.0;
float sws_chr_gblur = 0.0;
int sws_chr_vshift = 0;
int sws_chr_hshift = 0;
float sws_chr_sharpen = 0.0;
float sws_lum_sharpen = 0.0;

//global srcFilter
static SwsFilter *src_filter = NULL;

void sws_getFlagsAndFilterFromCmdLine(int *flags, SwsFilter **srcFilterParam,
                                      SwsFilter **dstFilterParam)
{
    static int firstTime = 1;
    *flags = 0;

    if (firstTime) {
        firstTime = 0;
        *flags = SWS_PRINT_INFO;
    } else if (mp_msg_test(MSGT_VFILTER, MSGL_DBG2))
        *flags = SWS_PRINT_INFO;

    if (src_filter)
        sws_freeFilter(src_filter);

    src_filter = sws_getDefaultFilter(
        sws_lum_gblur, sws_chr_gblur,
        sws_lum_sharpen, sws_chr_sharpen,
        sws_chr_hshift, sws_chr_vshift, verbose > 1);

    switch (sws_flags) {
    case 0: *flags |= SWS_FAST_BILINEAR;
        break;
    case 1: *flags |= SWS_BILINEAR;
        break;
    case 2: *flags |= SWS_BICUBIC;
        break;
    case 3: *flags |= SWS_X;
        break;
    case 4: *flags |= SWS_POINT;
        break;
    case 5: *flags |= SWS_AREA;
        break;
    case 6: *flags |= SWS_BICUBLIN;
        break;
    case 7: *flags |= SWS_GAUSS;
        break;
    case 8: *flags |= SWS_SINC;
        break;
    case 9: *flags |= SWS_LANCZOS;
        break;
    case 10: *flags |= SWS_SPLINE;
        break;
    default: *flags |= SWS_BILINEAR;
        break;
    }

    *srcFilterParam = src_filter;
    *dstFilterParam = NULL;
}

// will use sws_flags & src_filter (from cmd line)
static struct SwsContext *sws_getContextFromCmdLine2(int srcW, int srcH,
                                                     int srcFormat, int dstW,
                                                     int dstH, int dstFormat,
                                                     int extraflags)
{
    int flags;
    SwsFilter *dstFilterParam, *srcFilterParam;
    enum PixelFormat dfmt, sfmt;

    dfmt = imgfmt2pixfmt(dstFormat);
    sfmt = imgfmt2pixfmt(srcFormat);
    if (srcFormat == IMGFMT_RGB8 || srcFormat == IMGFMT_BGR8)
        sfmt = PIX_FMT_PAL8;
    sws_getFlagsAndFilterFromCmdLine(&flags, &srcFilterParam, &dstFilterParam);

    return sws_getContext(srcW, srcH, sfmt, dstW, dstH, dfmt, flags |
                          extraflags, srcFilterParam, dstFilterParam,
                          NULL);
}

struct SwsContext *sws_getContextFromCmdLine(int srcW, int srcH, int srcFormat,
                                             int dstW, int dstH,
                                             int dstFormat)
{
    return sws_getContextFromCmdLine2(srcW, srcH, srcFormat, dstW, dstH,
                                      dstFormat,
                                      0);
}

struct SwsContext *sws_getContextFromCmdLine_hq(int srcW, int srcH,
                                                int srcFormat, int dstW,
                                                int dstH,
                                                int dstFormat)
{
    return sws_getContextFromCmdLine2(
               srcW, srcH, srcFormat, dstW, dstH, dstFormat,
               SWS_FULL_CHR_H_INT | SWS_FULL_CHR_H_INP |
               SWS_ACCURATE_RND | SWS_BITEXACT);
}

#define SWS_MIN_BITS (16 * 8) // libswscale currently requires 16 bytes alignment
void mp_image_get_supported_regionstep(int *sx, int *sy,
                                       const struct mp_image *img)
{
    int p;

    if (img->chroma_x_shift == 31)
        *sx = 1;
    else
        *sx = (1 << img->chroma_x_shift);

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
        sws_getContextFromCmdLine_hq(src->w, src->h, src->imgfmt,
                                     dst->w, dst->h,
                                     dst->imgfmt);
    struct mp_csp_details mycsp = MP_CSP_DETAILS_DEFAULTS;
    if (csp)
        mycsp = *csp;
    mp_sws_set_colorspace(sws, &mycsp);
    sws_scale(sws, (const unsigned char *const *) src->planes, src->stride,
              0, src->h,
              dst->planes, dst->stride);
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
