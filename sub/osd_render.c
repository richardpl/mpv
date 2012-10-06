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

#include "sub/sub.h"
#include "libmpcodecs/mp_image.h"
#include "libmpcodecs/mp_image_utils.h"
#include "libmpcodecs/img_format.h"
#include "libvo/csputils.h"

void osd_render_to_mp_image(struct mp_image *dst, struct sub_bitmaps *sbs, struct mp_csp_details *csp)
{
    int i, x, y;
    int firstRow = dst->h;
    int endRow = 0;
    int color_yuv[3];
    int color_a;
    float yuv2rgb[3][4];
    float rgb2yuv[3][4];
    struct mp_csp_params cspar = { .colorspace = *csp, .brightness = 0, .contrast = 1, .hue = 0, .saturation = 1, .rgamma = 1, .ggamma = 1, .bgamma = 1, .texture_bits = 8, .input_bits = 8 };
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
    for (i = 0; i < sbs->num_parts; ++i) {
        struct sub_bitmap *sb = &sbs->parts[i];
        if (sb->y < firstRow)
            firstRow = sb->y;
        if (sb->y + sb->dh > endRow)
            endRow = sb->y + sb->dh;
    }
    firstRow &= ~((1 << dst->chroma_y_shift) - 1);
    --endRow;
    endRow |= (1 << dst->chroma_y_shift) - 1;
    ++endRow;

    if (firstRow < 0)
        firstRow = 0;
    if (endRow > dst->h)
        endRow = dst->h;
    if (firstRow >= endRow)
        return; // nothing to do

    // allocate temp image
    mp_image_t *temp = alloc_mpi(dst->w, endRow - firstRow, format);

    // convert to temp image
    mp_image_swscale_rows(temp, 0, endRow - firstRow, 1, dst, firstRow, endRow - firstRow, 1, csp);

    for (i = 0; i < sbs->num_parts; ++i) {
        struct sub_bitmap *sb = &sbs->parts[i];
        mp_image_t *sbi = NULL;
        mp_image_t *sba = NULL;

        // cut off areas outside the image
        int dst_x = sb->x;
        int dst_y = sb->y;
        int dst_w = sb->dw;
        int dst_h = sb->dh;
        if (dst_x < 0) {
            dst_w += dst_x;
            dst_x = 0;
        }
        if (dst_y < 0) {
            dst_h += dst_y;
            dst_y = 0;
        }
        if (dst_x + dst_w > dst->w) {
            dst_w = dst->w - dst_x;
        }
        if (dst_y + dst_h > dst->h) {
            dst_h = dst->h - dst_y;
        }

        // return if nothing left
        if (dst_w <= 0 || dst_h <= 0)
            continue;

        if (sbs->format == SUBBITMAP_RGBA && sb->w >= 8) { // >= 8 because of libswscale madness
            // swscale the bitmap from w*h to dw*dh, changing BGRA8 into YUV444P16 and make a scaled copy of A8
            mp_image_t *sbisrc = new_mp_image(sb->w, sb->h);
            mp_image_setfmt(sbisrc, IMGFMT_BGRA);
            sbisrc->planes[0] = sb->bitmap;
            sbi = alloc_mpi(sb->dw, sb->dh, format);
            mp_image_swscale_rows(sbi, 0, sb->dh, 1, sbisrc, 0, sb->h, 1, csp);
            free_mp_image(sbisrc);

            mp_image_t *sbasrc = alloc_mpi(sb->w, sb->h, IMGFMT_Y8);
            for (y = 0; y < sb->h; ++y)
                for (x = 0; x < sb->w; ++x)
                    sbasrc->planes[0][x + y * sbasrc->stride[0]] = ((unsigned char *) sb->bitmap)[(x + y * sb->stride) * 4 + 3];
            sba = alloc_mpi(sb->dw, sb->dh, IMGFMT_Y8);
            mp_image_swscale_rows(sba, 0, sb->dh, 1, sbasrc, 0, sb->h, 1, csp);
            free_mp_image(sbasrc);
            memset(color_yuv, 0, sizeof(color_yuv));
            color_a = 255;
        } else if (sbs->format == SUBBITMAP_LIBASS && !sbs->scaled) {
            // swscale alpha only
            sba = new_mp_image(sb->w, sb->h);
            mp_image_setfmt(sba, IMGFMT_Y8);
            sba->planes[0] = sb->bitmap;
            sba->stride[0] = sb->stride;
            int r = (sb->libass.color >> 24) & 0xFF;
            int g = (sb->libass.color >> 16) & 0xFF;
            int b = (sb->libass.color >> 8) & 0xFF;
            int a = sb->libass.color & 0xFF;
            color_yuv[0] = rint(MP_MAP_RGB2YUV_COLOR(rgb2yuv, r, g, b, 255, 0) * (bytes == 2 ? 257 : 1));
            color_yuv[1] = rint(MP_MAP_RGB2YUV_COLOR(rgb2yuv, r, g, b, 255, 1) * (bytes == 2 ? 257 : 1));
            color_yuv[2] = rint(MP_MAP_RGB2YUV_COLOR(rgb2yuv, r, g, b, 255, 2) * (bytes == 2 ? 257 : 1));
            color_a = 255 - a;
            // NOTE: these overflows can actually happen (when subtitles use color 0,0,0 while output levels only allows 16,16,16 upwards...)
            if(color_yuv[0] < 0)
                color_yuv[0] = 0;
            if(color_yuv[1] < 0)
                color_yuv[1] = 0;
            if(color_yuv[2] < 0)
                color_yuv[2] = 0;
            if(color_a < 0)
                color_a = 0;
            if(color_yuv[0] > (bytes == 2 ? 65535 : 255))
                color_yuv[0] = (bytes == 2 ? 65535 : 255);
            if(color_yuv[1] > (bytes == 2 ? 65535 : 255))
                color_yuv[1] = (bytes == 2 ? 65535 : 255);
            if(color_yuv[2] > (bytes == 2 ? 65535 : 255))
                color_yuv[2] = (bytes == 2 ? 65535 : 255);
            if(color_a > 255)
                color_a = 255;
        } else {
            mp_msg(MSGT_VO, MSGL_ERR, "render_sub_bitmap: invalid sub bitmap type\n");
            continue;
        }

        // call mp_image_blend_plane_with_alpha 3 times
        int p;
        for(p = 0; p < 3; ++p)
            mp_image_blend_plane_with_alpha(
                    (temp->planes[p] + (dst_y - firstRow) * temp->stride[p]) + dst_x * bytes,
                    temp->stride[p],
                    sbi ? sbi->planes[p] + (dst_y - sb->y) * sbi->stride[p] + (dst_x - sb->x) * bytes : NULL,
                    sbi ? sbi->stride[p] : 0,
                    color_yuv[p],
                    sba->planes[0] + (dst_y - sb->y) * sba->stride[0] + (dst_x - sb->x),
                    sba->stride[0],
                    color_a,
                    dst_h, dst_w, bytes
                    );

        if (sbi)
            free_mp_image (sbi);
        if (sba)
            free_mp_image (sba);
    }

    // convert back
    mp_image_swscale_rows(dst, firstRow, endRow - firstRow, 1, temp, 0, endRow - firstRow, 1, csp);

    // clean up
    free_mp_image(temp);
}

