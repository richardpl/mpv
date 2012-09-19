/*
 * This file is part of mplayer2.
 *
 * mplayer2 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mplayer2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with mplayer2; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "config.h"

#include "osdep/io.h"

#include "talloc.h"
#include "screenshot.h"
#include "mp_core.h"
#include "m_property.h"
#include "bstr.h"
#include "mp_msg.h"
#include "metadata.h"
#include "path.h"
#include "libmpcodecs/mp_image.h"
#include "libmpcodecs/dec_video.h"
#include "libmpcodecs/vf.h"
#include "libvo/video_out.h"
#include "image_writer.h"

#include "libvo/csputils.h"

#define MODE_FULL_WINDOW 1
#define MODE_SUBTITLES 2

typedef struct screenshot_ctx {
    struct MPContext *mpctx;

    int mode;
    int each_frame;
    int using_vf_screenshot;

    int frameno;
} screenshot_ctx;

void screenshot_init(struct MPContext *mpctx)
{
    mpctx->screenshot_ctx = talloc(mpctx, screenshot_ctx);
    *mpctx->screenshot_ctx = (screenshot_ctx) {
        .mpctx = mpctx,
        .frameno = 1,
    };
}

static char *stripext(void *talloc_ctx, const char *s)
{
    const char *end = strrchr(s, '.');
    if (!end)
        end = s + strlen(s);
    return talloc_asprintf(talloc_ctx, "%.*s", end - s, s);
}

static char *do_format_property(struct MPContext *mpctx, struct bstr s) {
    struct bstr prop_name = s;
    int fallbackpos = bstrchr(s, ':');
    if (fallbackpos >= 0)
        prop_name = bstr_splice(prop_name, 0, fallbackpos);
    char *pn = bstrdup0(NULL, prop_name);
    char *res = mp_property_print(pn, mpctx);
    talloc_free(pn);
    if (!res && fallbackpos >= 0)
        res = bstrdup0(NULL, bstr_cut(s, fallbackpos + 1));
    return res;
}

#ifdef _WIN32
#define ILLEGAL_FILENAME_CHARS "?\"/\\<>*|:"
#else
#define ILLEGAL_FILENAME_CHARS "/"
#endif

// Replace all characters disallowed in filenames with '_' and return the newly
// allocated result string.
static char *sanitize_filename(void *talloc_ctx, const char *s)
{
    char *res = talloc_strdup(talloc_ctx, s);
    char *cur = res;
    while (*cur) {
        if (strchr(ILLEGAL_FILENAME_CHARS, *cur) || ((unsigned char)*cur) < 32)
            *cur = '_';
        cur++;
    }
    return res;
}

static void append_filename(char **s, const char *f)
{
    char *append = sanitize_filename(NULL, f);
    *s = talloc_strdup_append(*s, append);
    talloc_free(append);
}

static char *create_fname(struct MPContext *mpctx, char *template,
                          const char *file_ext, int *sequence, int *frameno)
{
    char *res = talloc_strdup(NULL, ""); //empty string, non-NULL context

    time_t raw_time = time(NULL);
    struct tm *local_time = localtime(&raw_time);

    if (!template || *template == '\0')
        template = "shot%n";

    for (;;) {
        char *next = strchr(template, '%');
        if (!next)
            break;
        res = talloc_strndup_append(res, template, next - template);
        template = next + 1;
        char fmt = *template++;
        switch (fmt) {
        case '#':
        case '0':
        case 'n': {
            int digits = '4';
            if (fmt == '#') {
                if (!*sequence) {
                    *frameno = 1;
                }
                fmt = *template++;
            }
            if (fmt == '0') {
                digits = *template++;
                if (digits < '0' || digits > '9')
                    goto error_exit;
                fmt = *template++;
            }
            if (fmt != 'n')
                goto error_exit;
            char fmtstr[] = {'%', '0', digits, 'd', '\0'};
            res = talloc_asprintf_append(res, fmtstr, *frameno);
            if (*frameno < 100000 - 1) {
                (*frameno) += 1;
                (*sequence) += 1;
            }
            break;
        }
        case 'f':
        case 'F': {
            char *video_file = get_metadata(mpctx, META_NAME);
            if (video_file) {
                char *name = video_file;
                if (fmt == 'F')
                    name = stripext(res, video_file);
                append_filename(&res, name);
            }
            talloc_free(video_file);
            break;
        }
        case 'p':
        case 'P': {
            char *t = mp_format_time(get_current_time(mpctx), fmt == 'P');
            append_filename(&res, t);
            talloc_free(t);
            break;
        }
        case 't': {
            char fmt = *template;
            if (!fmt)
                goto error_exit;
            template++;
            char fmtstr[] = {'%', fmt, '\0'};
            char buffer[80];
            if (strftime(buffer, sizeof(buffer), fmtstr, local_time) == 0)
                buffer[0] = '\0';
            append_filename(&res, buffer);
            break;
        }
        case '{': {
            char *end = strchr(template, '}');
            if (!end)
                goto error_exit;
            struct bstr prop = bstr_splice(bstr0(template), 0, end - template);
            template = end + 1;
            char *s = do_format_property(mpctx, prop);
            if (s)
                append_filename(&res, s);
            talloc_free(s);
            break;
        }
        case '%':
            res = talloc_strdup_append(res, "%");
            break;
        default:
            goto error_exit;
        }
    }

    res = talloc_strdup_append(res, template);
    return talloc_asprintf_append(res, ".%s", file_ext);

error_exit:
    talloc_free(res);
    return NULL;
}

static char *gen_fname(screenshot_ctx *ctx, const char *file_ext)
{
    int sequence = 0;
    for (;;) {
        int prev_sequence = sequence;
        char *fname = create_fname(ctx->mpctx,
                                   ctx->mpctx->opts.screenshot_template,
                                   file_ext,
                                   &sequence,
                                   &ctx->frameno);

        if (!fname) {
            mp_msg(MSGT_CPLAYER, MSGL_ERR, "Invalid screenshot filename "
                   "template! Fix or remove the --screenshot-template option."
                   "\n");
            return NULL;
        }

        if (!mp_path_exists(fname))
            return fname;

        if (sequence == prev_sequence) {
            mp_msg(MSGT_CPLAYER, MSGL_ERR, "Can't save screenshot, file '%s' "
                   "already exists!\n", fname);
            talloc_free(fname);
            return NULL;
        }

        talloc_free(fname);
    }
}

#ifdef CONFIG_ASS

#include "sub/ass_mp.h"
#include "sub/sub.h"

static void eosd_draw_alpha_rgb24(unsigned char *src,
                                 int src_w, int src_h, int src_stride,
                                 unsigned char *dst,
                                 size_t dst_stride,
                                 int dst_x, int dst_y,
                                 uint32_t color)
{
    const unsigned int r = (color >> 24) & 0xff;
    const unsigned int g = (color >> 16) & 0xff;
    const unsigned int b = (color >>  8) & 0xff;
    const unsigned int a = 0xff - (color & 0xff);

    dst += dst_y * dst_stride + dst_x * 3;

    for (int y = 0; y < src_h; y++, dst += dst_stride, src += src_stride) {
        for (int x = 0; x < src_w; x++) {
            unsigned char k = (src[x] * a + 255) >> 8;
            dst[3*x + 0] = (r * k + dst[3*x + 0] * (0xff - k) + 255) >> 8;
            dst[3*x + 1] = (g * k + dst[3*x + 1] * (0xff - k) + 255) >> 8;
            dst[3*x + 2] = (b * k + dst[3*x + 2] * (0xff - k) + 255) >> 8;
        }
    }
}

static void eosd_render_rgb24(unsigned char *image, size_t stride, int w, int h,
                              ASS_Image *imgs)
{
    for (ASS_Image *p = imgs; p; p = p->next) {
        eosd_draw_alpha_rgb24(p->bitmap, p->w, p->h, p->stride,
                              image, stride,
                              p->dst_x, p->dst_y, p->color);
    }
}

static struct mp_image *render_subs(struct MPContext *mpctx,
                                    struct mp_image *image,
                                    struct mp_csp_details *colorspace)
{
    struct osd_state *osd = mpctx->osd;

    osd->dim = (struct mp_eosd_res) { .w = image->w, .h = image->h };
    osd->normal_scale = 1;
    osd->vsfilter_scale = 1;
    osd->support_rgba = false;
    struct sub_bitmaps b;
    sub_get_bitmaps(osd, &b);

    if (!b.imgs)
        return image;

    image = convert_image_maybe(image, colorspace, IMGFMT_RGB24);
    assert(image->imgfmt == IMGFMT_RGB24);

    eosd_render_rgb24(image->planes[0], image->stride[0], image->w, image->h,
                      b.imgs);

    return image;
}

#endif

static void screenshot_save(struct MPContext *mpctx, struct mp_image *image,
                            int mode)
{
    screenshot_ctx *ctx = mpctx->screenshot_ctx;

    struct mp_csp_details colorspace;
    get_detected_video_colorspace(mpctx->sh_video, &colorspace);

    struct image_writer_opts *opts = mpctx->opts.screenshot_image_opts;
    struct mp_image *new_image = image;

#ifdef CONFIG_ASS
    if (mode == MODE_SUBTITLES)
        new_image = render_subs(mpctx, image, &colorspace);
#endif

    char *filename = gen_fname(ctx, image_writer_file_ext(opts));
    if (filename) {
        mp_msg(MSGT_CPLAYER, MSGL_INFO, "*** screenshot '%s' ***\n", filename);
        if (!write_image(new_image, &colorspace, opts, filename))
            mp_msg(MSGT_CPLAYER, MSGL_ERR, "\nError writing screenshot!\n");
        talloc_free(filename);
    }

    if (new_image != image)
        free_mp_image(new_image);
}

static void vf_screenshot_callback(void *pctx, struct mp_image *image)
{
    struct MPContext *mpctx = (struct MPContext *)pctx;
    screenshot_ctx *ctx = mpctx->screenshot_ctx;
    screenshot_save(mpctx, image, ctx->mode);
    if (ctx->each_frame)
        screenshot_request(mpctx, 0, ctx->mode);
}

static bool video_filter_active(struct MPContext *mpctx, const char *name)
{
    if (mpctx->sh_video) {
        struct vf_instance *vf = mpctx->sh_video->vfilter;
        while (vf) {
            if (strcmp(vf->info->name, name) == 0)
                return true;
            vf = vf->next;
        }
    }
    return false;
}

void screenshot_request(struct MPContext *mpctx, bool each_frame, int mode)
{
    if (mpctx->video_out && mpctx->video_out->config_ok) {
        screenshot_ctx *ctx = mpctx->screenshot_ctx;

        ctx->using_vf_screenshot = 0;

        if (mode == MODE_SUBTITLES && video_filter_active(mpctx, "ass"))
            mode = 0;

        if (each_frame) {
            ctx->each_frame = !ctx->each_frame;
            ctx->mode = mode;
            if (!ctx->each_frame)
                return;
        }

        struct voctrl_screenshot_args args =
                            { .full_window = (mode == MODE_FULL_WINDOW) };
        if (!video_filter_active(mpctx, "screenshot_force")
            && vo_control(mpctx->video_out, VOCTRL_SCREENSHOT, &args) == true)
        {
            screenshot_save(mpctx, args.out_image, mode);
            free_mp_image(args.out_image);
        } else {
            mp_msg(MSGT_CPLAYER, MSGL_INFO, "No VO support for taking"
                   " screenshots, trying VFCTRL_SCREENSHOT!\n");
            ctx->using_vf_screenshot = 1;
            struct vf_ctrl_screenshot cmd = {
                .image_callback = vf_screenshot_callback,
                .image_callback_ctx = mpctx,
            };
            struct vf_instance *vfilter = mpctx->sh_video->vfilter;
            if (vfilter->control(vfilter, VFCTRL_SCREENSHOT, &cmd) !=
                    CONTROL_OK)
                mp_msg(MSGT_CPLAYER, MSGL_INFO,
                       "...failed (need --vf=screenshot?)\n");
        }
    }
}

void screenshot_flip(struct MPContext *mpctx)
{
    screenshot_ctx *ctx = mpctx->screenshot_ctx;

    if (!ctx->each_frame)
        return;

    // screenshot_flip is called when the VO presents a new frame. vf_screenshot
    // can behave completely different (consider filters inserted between
    // vf_screenshot and vf_vo, that add or remove frames), so handle this case
    // somewhere else.
    if (ctx->using_vf_screenshot)
        return;

    screenshot_request(mpctx, 0, ctx->mode);
}
