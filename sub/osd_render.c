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
#include "libmpcodecs/mp_image_utils.h"
#include "libmpcodecs/img_format.h"
#include "libvo/csputils.h"

static bool sub_bitmap_to_mp_images(struct mp_image **sbi, int *color_yuv,
                                    int *color_a, struct mp_image **sba,
                                    struct sub_bitmap *sb,
                                    int format, struct mp_csp_details *csp,
                                    float rgb2yuv[3][4], int bytes)
{
    int x, y;
    *sbi = NULL;
    *sba = NULL;
    if (format == SUBBITMAP_RGBA && sb->w >= 8) { // >= 8 because of libswscale madness
        // swscale the bitmap from w*h to dw*dh, changing BGRA8 into YUV444P16 and make a scaled copy of A8
        mp_image_t *sbisrc = new_mp_image(sb->w, sb->h);
        mp_image_setfmt(sbisrc, IMGFMT_BGRA);
        sbisrc->planes[0] = sb->bitmap;
        *sbi = alloc_mpi(sb->dw, sb->dh, format);
        mp_image_swscale(*sbi, sbisrc, csp);
        free_mp_image(sbisrc);

        mp_image_t *sbasrc = alloc_mpi(sb->w, sb->h, IMGFMT_Y8);
        for (y = 0; y < sb->h; ++y)
            for (x = 0; x < sb->w; ++x)
                sbasrc->planes[0][x + y *
                                  sbasrc->stride[0]] =
                    ((unsigned char *) sb->bitmap)[(x + y *
                                                    sb->stride) * 4 + 3];
        *sba = alloc_mpi(sb->dw, sb->dh, IMGFMT_Y8);
        mp_image_swscale(*sba, sbasrc, csp);
        free_mp_image(sbasrc);
        color_yuv[0] = 255;
        color_yuv[1] = 128;
        color_yuv[2] = 128;
        *color_a = 255;
        return true;
    } else if (format == SUBBITMAP_LIBASS && sb->w == sb->dw && sb->h ==
               sb->dh) {
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
            rint(MP_MAP_RGB2YUV_COLOR(rgb2yuv, r, g, b, 255,
                                      0) * (bytes == 2 ? 257 : 1));
        color_yuv[1] =
            rint(MP_MAP_RGB2YUV_COLOR(rgb2yuv, r, g, b, 255,
                                      1) * (bytes == 2 ? 257 : 1));
        color_yuv[2] =
            rint(MP_MAP_RGB2YUV_COLOR(rgb2yuv, r, g, b, 255,
                                      2) * (bytes == 2 ? 257 : 1));
        *color_a = 255 - a;
        // NOTE: these overflows can actually happen (when subtitles use color 0,0,0 while output levels only allows 16,16,16 upwards...)
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

static bool clip_to_bounds(int *x, int *y, int *w, int *h, int bx, int by, int bw, int bh)
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
        return false; // nothing left

    return true;
}

void osd_render_to_mp_image(struct mp_image *dst, struct sub_bitmaps *sbs,
                            struct mp_csp_details *csp)
{
    int i;
    int x1, y1, x2, y2;
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
    y1 &= ~((1 << dst->chroma_y_shift) - 1);
    --y2;
    y2 |= (1 << dst->chroma_y_shift) - 1;
    ++y2;

    if (y1 < 0)
        y1 = 0;
    if (y2 > dst->h)
        y2 = dst->h;
    if (y1 >= y2)
        return;  // nothing to do

    // convert to a temp image
    mp_image_t *temp = alloc_mpi(dst->w, y2 - y1, format);
    mp_image_swscale_region(temp, 0, 0, temp->w, y2 - y1, 1, dst, 0, y1, dst->w, y2 - y1, 1, csp);

    for (i = 0; i < sbs->num_parts; ++i) {
        struct sub_bitmap *sb = &sbs->parts[i];
        mp_image_t *sbi = NULL;
        mp_image_t *sba = NULL;

        // cut off areas outside the image
        int dst_x = sb->x;
        int dst_y = sb->y - y1; // relative to temp, please!
        int dst_w = sb->dw;
        int dst_h = sb->dh;
	if (!clip_to_bounds(&dst_x, &dst_y, &dst_w, &dst_h, 0, 0, temp->w, temp->h))
            continue;

        if (!sub_bitmap_to_mp_images(&sbi, color_yuv, &color_a, &sba, sb,
                                     sbs->format, csp, rgb2yuv, bytes)) {
            mp_msg(MSGT_VO, MSGL_ERR,
                   "render_sub_bitmap: invalid sub bitmap type\n");
            continue;
        }

        // call mp_blend_alpha 3 times
        int p;
        for (p = 0; p < 3; ++p) {
            unsigned char *dst_p =
                temp->planes[p] + dst_y * temp->stride[p] + dst_x * bytes;
            int src_x = dst_x        - sb->x;
            int src_y = (dst_y + y1) - sb->y;
            unsigned char *alpha_p =
                sba->planes[0] + src_y * sba->stride[0] + src_x;
	    if (sbi) {
                unsigned char *src_p =
                    sbi->planes[p] + src_y * sbi->stride[p] + src_x * bytes;
                mp_blend_src_alpha(
                    dst_p, temp->stride[p],
                    src_p, sbi ? sbi->stride[p] : 0,
                    alpha_p, sba->stride[0], color_a,
                    dst_h, dst_w, bytes
                    );
            } else {
                mp_blend_const_alpha(
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
    mp_image_swscale_region(dst, 0, y1, dst->w, y2 - y1, 1, temp, 0, 0, temp->w, y2 - y1, 1, csp);

    // clean up
    free_mp_image(temp);
}
