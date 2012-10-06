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

#include "sub/osd_render.h"

#include <stdbool.h>

#include "sub/sub.h"
#include "libmpcodecs/mp_image.h"
#include "libmpcodecs/sws_utils.h"
#include "libmpcodecs/img_format.h"
#include "libvo/csputils.h"

static void blend_const16_alpha(uint8_t *dst,
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
            uint32_t srcap = srcar[j];
                // 32bit to force the math ops to operate on 32 bit
            srcap *= srcamul; // now 0..65025
            uint16_t outp =
                (srcp * srcap * srcamul + dstp *
                 (65025 - srcap) + 32512) / 65025;
            dstr[j] = outp;
        }
    }
}

static void blend_src16_alpha(uint8_t *dst,
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
            uint32_t srcap = srcar[j];
                // 32bit to force the math ops to operate on 32 bit
            srcap *= srcamul; // now 0..65025
            uint16_t outp =
                (srcp * srcamul +
                 127) / 255 + (dstp * (65025 - srcap) + 32512) / 65025;
                // premultiplied alpha GL_ONE GL_ONE_MINUS_SRC_ALPHA
            dstr[j] = outp;
        }
    }
}

static void blend_const8_alpha(uint8_t *dst,
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
            uint32_t srcap = srcar[j];
                // 32bit to force the math ops to operate on 32 bit
            srcap *= srcamul; // now 0..65025
            uint8_t outp =
                (srcp * srcap + dstp * (65025 - srcap) + 32512) / 65025;
            dstr[j] = outp;
        }
    }
}

static void blend_src8_alpha(uint8_t *dst,
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
            uint32_t srcap = srcar[j];
                // 32bit to force the math ops to operate on 32 bit
            srcap *= srcamul; // now 0..65025
            uint8_t outp =
                (srcp * srcamul +
                 127) / 255 + (dstp * (65025 - srcap) + 32512) / 65025; 
                // premultiplied alpha GL_ONE GL_ONE_MINUS_SRC_ALPHA
            dstr[j] = outp;
        }
    }
}

static void blend_src_alpha(uint8_t *dst, ssize_t dstRowStride,
                            const uint8_t *src, ssize_t srcRowStride,
                            const uint8_t *srca, ssize_t srcaRowStride,
                            uint8_t srcamul,
                            int rows, int cols, int bytes)
{
    if (bytes == 2) {
        blend_src16_alpha(dst, dstRowStride, src,
                          srcRowStride, srca,
                          srcaRowStride, srcamul, rows,
                          cols);
    } else if (bytes == 1) {
        blend_src8_alpha(dst, dstRowStride, src,
                         srcRowStride, srca,
                         srcaRowStride, srcamul, rows,
                         cols);
    }
}

static void blend_const_alpha(uint8_t *dst, ssize_t dstRowStride,
                              uint8_t srcp,
                              const uint8_t *srca, ssize_t srcaRowStride,
                              uint8_t srcamul,
                              int rows, int cols, int bytes)
{
    if (bytes == 2) {
        blend_const16_alpha(dst, dstRowStride, srcp,
                            srca, srcaRowStride,
                            srcamul, rows,
                            cols);
    } else if (bytes == 1) {
        blend_const8_alpha(dst, dstRowStride, srcp,
                           srca, srcaRowStride, srcamul,
                           rows,
                           cols);
    }
}

static bool sub_bitmap_to_mp_images(struct mp_image **sbi, int *color_yuv,
                                    int *color_a, struct mp_image **sba,
                                    struct sub_bitmap *sb,
                                    int format, struct mp_csp_details *csp,
                                    float rgb2yuv[3][4], int bytes)
{
    int x, y;
    *sbi = NULL;
    *sba = NULL;
    if (format == SUBBITMAP_RGBA && sb->w >= 8) {
        // >= 8 because of libswscale madness
        // swscale the bitmap from w*h to dw*dh, changing BGRA8 into YUV444P16
        // and make a scaled copy of A8
        mp_image_t *sbisrc = new_mp_image(sb->w, sb->h);
        mp_image_setfmt(sbisrc, IMGFMT_BGRA);
        sbisrc->planes[0] = sb->bitmap;
        *sbi = alloc_mpi(sb->dw, sb->dh, format);
        mp_image_swscale(*sbi, sbisrc, csp);
        free_mp_image(sbisrc);

        mp_image_t *sbasrc = alloc_mpi(sb->w, sb->h, IMGFMT_Y8);
        for (y = 0; y < sb->h; ++y)
            for (x = 0; x < sb->w; ++x)
                sbasrc->planes[0][
                        x + y * sbasrc->stride[0]
                    ] =
                    ((unsigned char *) sb->bitmap)[
                        (x + y * sb->stride) * 4 + 3
                    ];
        *sba = alloc_mpi(sb->dw, sb->dh, IMGFMT_Y8);
        mp_image_swscale(*sba, sbasrc, csp);
        free_mp_image(sbasrc);
        color_yuv[0] = 255;
        color_yuv[1] = 128;
        color_yuv[2] = 128;
        *color_a = 255;
        return true;
    } else if (format == SUBBITMAP_LIBASS &&
            sb->w == sb->dw && sb->h == sb->dh) {
        // swscale alpha only
        *sba = new_mp_image(sb->w, sb->h);
        mp_image_setfmt(*sba, IMGFMT_Y8);
        (*sba)->planes[0] = sb->bitmap;
        (*sba)->stride[0] = sb->stride;
        int r = (sb->libass.color >> 24) & 0xFF;
        int g = (sb->libass.color >> 16) & 0xFF;
        int b = (sb->libass.color >> 8) & 0xFF;
        int a = sb->libass.color & 0xFF;
        color_yuv[0] =
            rint(MP_MAP_RGB2YUV_COLOR(rgb2yuv, r, g, b, 255, 0)
                    * (bytes == 2 ? 257 : 1));
        color_yuv[1] =
            rint(MP_MAP_RGB2YUV_COLOR(rgb2yuv, r, g, b, 255, 1)
                    * (bytes == 2 ? 257 : 1));
        color_yuv[2] =
            rint(MP_MAP_RGB2YUV_COLOR(rgb2yuv, r, g, b, 255, 2)
                    * (bytes == 2 ? 257 : 1));
        *color_a = 255 - a;
        // NOTE: these overflows can actually happen (when subtitles use color
        // 0,0,0 while output levels only allows 16,16,16 upwards...)
        if (color_yuv[0] < 0)
            color_yuv[0] = 0;
        if (color_yuv[1] < 0)
            color_yuv[1] = 0;
        if (color_yuv[2] < 0)
            color_yuv[2] = 0;
        if (*color_a < 0)
            *color_a = 0;
        if (color_yuv[0] > (bytes == 2 ? 65535 : 255))
            color_yuv[0] = (bytes == 2 ? 65535 : 255);
        if (color_yuv[1] > (bytes == 2 ? 65535 : 255))
            color_yuv[1] = (bytes == 2 ? 65535 : 255);
        if (color_yuv[2] > (bytes == 2 ? 65535 : 255))
            color_yuv[2] = (bytes == 2 ? 65535 : 255);
        if (*color_a > 255)
            *color_a = 255;
        return true;
    } else
        return false;
}

static void mp_image_crop(struct mp_image *img, int x, int y, int w, int h)
{
    int p;
    for (p = 0; p < img->num_planes; ++p) {
        int bits = MP_IMAGE_BITS_PER_PIXEL_ON_PLANE(img, p);
        img->planes[p] += y * img->stride[p] + (x * bits) / 8;
    }
    img->w = w;
    img->h = h;
}

static bool clip_to_bounds(int *x, int *y, int *w, int *h,
                           int bx, int by, int bw, int bh)
{
    if (*x < bx) {
        *w += *x - bx;
        *x = bx;
    }
    if (*y < 0) {
        *h += *y - by;
        *y = by;
    }
    if (*x + *w > bx + bw)
        *w = bx + bw - *x;
    if (*y + *h > by + bh)
        *h = by + bh - *y;

    if (*w <= 0 || *h <= 0)
        return false;  // nothing left

    return true;
}

#define SWS_MIN_BITS (16 * 8) // libswscale currently requires 16 bytes alignment
static void get_swscale_requirements(int *sx, int *sy,
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

static void align_bbox(int *x1, int *y1, int *x2, int *y2, int xstep, int ystep)
{
    *x1 -= (*x1 % xstep);
    *y1 -= (*y1 % ystep);

    *x2 += xstep - 1;
    *y2 += ystep - 1;
    *x2 -= (*x2 % xstep);
    *y2 -= (*y2 % ystep);
}

bool align_bbox_to_swscale_requirements(int *x1, int *y1, int *x2, int *y2,
                                        struct mp_image *img)
{
    int xstep, int ystep;
    get_swscale_requirements(&xstep, &ystep, img);
    align_bbox(x1, y1, x2, y2, xstep, ystep);

    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;
    if (x2 > img->w)
        x2 = img->w;
    if (y2 > img->h)
        y2 = img->h;

    return (x2 > x1) && (y2 > y1);
}

void osd_render_to_mp_image(struct mp_image *dst, struct sub_bitmaps *sbs,
                            struct mp_csp_details *csp)
{
    int i;
    int x1, y1, x2, y2, xstep, ystep;
    int color_yuv[3];
    int color_a;
    float yuv2rgb[3][4];
    float rgb2yuv[3][4];
    struct mp_csp_params cspar = {
        .colorspace = *csp,
        .brightness = 0, .contrast = 1,
        .hue = 0, .saturation = 1,
        .rgamma = 1, .ggamma = 1, .bgamma = 1,
        .texture_bits = 8, .input_bits = 8
    };

#if 1
    int format = IMGFMT_444P16;
    int bytes = 2;
#else
    int format = IMGFMT_444P;
    int bytes = 1;
#endif

    // prepare YUV/RGB conversion values
    mp_get_yuv2rgb_coeffs(&cspar, yuv2rgb);
    mp_invert_yuv2rgb(rgb2yuv, yuv2rgb);

    // calculate bounding range
    if (!sub_bitmaps_bb(sbs, &x1, &y1, &x2, &y2))
        return;

    if (!align_bbox_to_swscale_requirements(&x1, &y1, &x2, &y2, dst))
        return;  // nothing to do

    // convert to a temp image
    mp_image_t *temp = alloc_mpi(x2 - x1, y2 - y1, format);
    mp_image_t dst_region = *dst;
    mp_image_crop(&dst_region, x1, y1, x2 - x1, y2 - y1);
    mp_image_swscale(temp, &dst_region, csp);

    for (i = 0; i < sbs->num_parts; ++i) {
        struct sub_bitmap *sb = &sbs->parts[i];
        mp_image_t *sbi = NULL;
        mp_image_t *sba = NULL;

        // cut off areas outside the image
        int dst_x = sb->x - x1; // coordinates are relative to the bbox
        int dst_y = sb->y - y1; // coordinates are relative to the bbox
        int dst_w = sb->dw;
        int dst_h = sb->dh;
        if (!clip_to_bounds(&dst_x, &dst_y, &dst_w, &dst_h,
                            0, 0, temp->w, temp->h))
            continue;

        if (!sub_bitmap_to_mp_images(&sbi, color_yuv, &color_a, &sba, sb,
                                     sbs->format, csp, rgb2yuv, bytes)) {
            mp_msg(MSGT_VO, MSGL_ERR,
                   "render_sub_bitmap: invalid sub bitmap type\n");
            continue;
        }

        // call blend_alpha 3 times
        int p;
        for (p = 0; p < 3; ++p) {
            unsigned char *dst_p =
                temp->planes[p] + dst_y * temp->stride[p] + dst_x * bytes;
            int src_x = (dst_x + x1) - sb->x;
            int src_y = (dst_y + y1) - sb->y;
            unsigned char *alpha_p =
                sba->planes[0] + src_y * sba->stride[0] + src_x;
            if (sbi) {
                unsigned char *src_p =
                    sbi->planes[p] + src_y * sbi->stride[p] + src_x * bytes;
                blend_src_alpha(
                    dst_p, temp->stride[p],
                    src_p, sbi->stride[p],
                    alpha_p, sba->stride[0], color_a,
                    dst_h, dst_w, bytes
                    );
            } else {
                blend_const_alpha(
                    dst_p, temp->stride[p],
                    color_yuv[p],
                    alpha_p, sba->stride[0], color_a,
                    dst_h, dst_w, bytes
                    );
            }
        }

        if (sbi)
            free_mp_image(sbi);
        if (sba)
            free_mp_image(sba);
    }

    // convert back
    mp_image_swscale(&dst_region, temp, csp);

    // clean up
    free_mp_image(temp);
}

// vim: ts=4 sw=4 et tw=80
