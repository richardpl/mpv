/*
 * This file is part of mplayer.
 *
 * mplayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mplayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with mplayer.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <luajit.h>

#include "config.h"
#include "mp_msg.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

#include "m_option.h"
#include "m_struct.h"

#define PLANES 3

static struct vf_priv_s {
    char *cfg_fn;
    char *cfg_plane_fn[PLANES];
    char *cfg_file;
    int cfg_rgb;
    char *cfg_lut;
    char *cfg_plane_lut[PLANES];
    char *cfg_config;

    int config_w, config_h;

    lua_State *L;
} const vf_priv_dflt = {};

static const char *lua_code =
#include "libmpcodecs/vf_lua_lib.h"
;

static int query_format(struct vf_instance *vf, unsigned int fmt)
{
    struct vf_priv_s *p = vf->priv;
    if (p->cfg_rgb) {
        if (fmt != IMGFMT_RGB32)
            return 0;
    } else {
        if (mp_get_chroma_shift(fmt, NULL, NULL, NULL) == 0)
            return 0;
    }
    return vf_next_query_format(vf, fmt);
}

static void print_call_error(lua_State *L, const char *what)
{
    const char *err = "<unknown>";
    if (lua_isstring(L, -1))
        err = lua_tostring(L, -1);
    mp_msg(MSGT_VFILTER, MSGL_ERR, "vf_lua: error running %s: %s\n", what, err);
    lua_pop(L, 1);
}

struct config_closure {
    int *w, *h, *d_w, *d_h;
};

static int run_lua_config(lua_State *L)
{
    struct config_closure *c = lua_touserdata(L, 1);

    lua_getglobal(L, "config");
    lua_pushinteger(L, *c->w);
    lua_pushinteger(L, *c->h);
    lua_pushinteger(L, *c->d_w);
    lua_pushinteger(L, *c->d_h);
    lua_call(L, 4, 4);
    *c->w = lua_tointeger(L, -4);
    *c->h = lua_tointeger(L, -3);
    *c->d_w = lua_tointeger(L, -2);
    *c->d_h = lua_tointeger(L, -1);

    return 0;
}

static int config(struct vf_instance *vf,
                  int width, int height, int d_width, int d_height,
                  unsigned int flags, unsigned int fmt)
{
    struct vf_priv_s *p = vf->priv;

    struct config_closure c = { &width, &height, &d_width, &d_height };
    if (lua_cpcall(p->L, run_lua_config, &c)) {
        print_call_error(p->L, "config");
        return 0;
    }

    p->config_w = width;
    p->config_h = height;

    return vf_next_config(vf, width, height, d_width, d_height, flags, fmt);
}

static void uninit(struct vf_instance *vf)
{
    struct vf_priv_s *p = vf->priv;
    lua_close(p->L);
}

// Set the field of the passed Lua table to the given mpi
// Lua stack: p (Lua table for image)
static void set_lua_mpi_fields(lua_State *L, mp_image_t *mpi)
{
    int chroma_xs = 0, chroma_ys = 0, bits, bpp;
    bool packed_rgb = mpi->imgfmt == IMGFMT_RGB32;
    if (packed_rgb) {
        bits = 8;
        bpp = 32;
    } else {
        mp_get_chroma_shift(mpi->imgfmt, &chroma_xs, &chroma_ys, &bits);
        bpp = bits;
    }

    for (int n = 0; n < PLANES; n++) {
        lua_pushinteger(L, n + 1); // p n
        lua_gettable(L, -2); // p ps

        bool valid = n < mpi->num_planes;

        lua_pushinteger(L, valid ? n + 1 : 0);
        lua_setfield(L, -2, "plane_nr");

        lua_pushlightuserdata(L, valid ? mpi->planes[n] : NULL);
        lua_setfield(L, -2, "ptr");

        lua_pushinteger(L, valid ? mpi->stride[n] : 0);
        lua_setfield(L, -2, "stride");

        bool chroma = (n == 1 || n == 2);
        int sx = chroma ? chroma_xs : 0;
        int sy = chroma ? chroma_ys : 0;

        lua_pushinteger(L, valid ? (mpi->w >> sx) : 0);
        lua_setfield(L, -2, "width");

        lua_pushinteger(L, valid ? (mpi->h >> sy) : 0);
        lua_setfield(L, -2, "height");

        const char *pixel_ptr_type;
        if (bpp <= 8) {
            pixel_ptr_type = "uint8_t*";
        } else if (bpp <= 16) {
            pixel_ptr_type = "uint16_t*";
        } else if (bpp <= 32) {
            pixel_ptr_type = "uint32_t*";
        } else {
            abort();
        }
        lua_pushstring(L, valid ? pixel_ptr_type : "");
        lua_setfield(L, -2, "pixel_ptr_type");

        lua_pushinteger(L, valid ? ((bpp + 7) / 8) : 0);
        lua_setfield(L, -2, "bytes_per_pixel");

        int max = (1 << bits) - 1;
        lua_pushinteger(L, valid ? max : 0);
        lua_setfield(L, -2, "max");

        lua_pushstring(L, mp_imgfmt_to_name(mpi->imgfmt));
        lua_setfield(L, -2, "imgfmt");

        lua_pushboolean(L, packed_rgb);
        lua_setfield(L, -2, "packed_rgb");

        lua_pop(L, 1); // p
    }

    lua_pushinteger(L, mpi->w);
    lua_setfield(L, -2, "width");

    lua_pushinteger(L, mpi->h);
    lua_setfield(L, -2, "height");

    lua_pushinteger(L, mpi->num_planes);
    lua_setfield(L, -2, "plane_count");
}

struct filter_closure {
    mp_image_t *mpi, *dmpi;
};

static int run_lua_filter(lua_State *L)
{
    struct filter_closure *c = lua_touserdata(L, 1);

    lua_getglobal(L, "src"); // p
    set_lua_mpi_fields(L, c->mpi); // p
    lua_pop(L, 1); // -

    lua_getglobal(L, "dst"); // p
    set_lua_mpi_fields(L, c->dmpi); // p
    lua_pop(L, 1); // -

    lua_getglobal(L, "_prepare_filter");
    lua_call(L, 0, 0);

    // Call the Lua filter function
    lua_getglobal(L, "filter_image");
    lua_call(L, 0, 0);

    return 0;
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
    struct vf_priv_s *p = vf->priv;
    lua_State *L = vf->priv->L;

    vf->dmpi = vf_get_image(vf->next,mpi->imgfmt, MP_IMGTYPE_TEMP,
                            MP_IMGFLAG_ACCEPT_STRIDE, p->config_w, p->config_h);

    mp_image_t *dmpi = vf->dmpi;

    vf_clone_mpi_attributes(dmpi, mpi);

    struct filter_closure c = { .mpi = mpi, .dmpi = dmpi };
    if (lua_cpcall(L, run_lua_filter, &c))
        print_call_error(L, "filter");

    return vf_next_put_image(vf, dmpi, pts);
}

static bool string_is_set(const char *arg)
{
    return arg && arg[0];
}

// Compile (_G.prefix .. expr), and return it on the Lua stack.
static bool load_fn(lua_State *L, const char *prefix, const char *expr)
{
    lua_getglobal(L, prefix); // s
    lua_pushstring(L, expr); // s*2
    lua_concat(L, 2); // s
    const char *s = lua_tostring(L, -1); // s
    if (luaL_loadstring(L, s))
        return false;
    // s fn
    lua_remove(L, -2); // fn
    return true;
}

// Do load_fn() for each plane (fall back to expr if no per-plane function set).
// Return the table of plane functions on the Lua stack.
static bool load_fn_planes(lua_State *L, const char *prefix, const char *expr,
                           char *expr_planes[3])
{
    lua_newtable(L); // t
    for (int n = 0; n < PLANES; n++) {
        const char *plane_expr = expr_planes[n];
        if (!string_is_set(plane_expr))
            plane_expr = expr;
        if (string_is_set(plane_expr)) {
            if (!load_fn(L, prefix, plane_expr))
                return false;
            lua_rawseti(L, -2, n + 1); // t
        }
    }
    return true;
}

static int vf_open(vf_instance_t *vf, char *args)
{
    struct vf_priv_s *p = vf->priv;
    lua_State *L = luaL_newstate();
    p->L = L;

    luaL_openlibs(L);

    if (luaL_dostring(L, lua_code))
        goto lua_error;

    if (!load_fn_planes(L, "PIXEL_FN_PRELUDE", p->cfg_fn, p->cfg_plane_fn))
        goto lua_error;
    lua_setglobal(L, "plane_fn"); // -

    if (!load_fn_planes(L, "LUT_FN_PRELUDE", p->cfg_lut, p->cfg_plane_lut))
        goto lua_error;
    lua_setglobal(L, "plane_lut_fn"); // -

    if (string_is_set(p->cfg_config)) {
        if (!load_fn(L, "CONFIG_FN_PRELUDE", p->cfg_config))
            goto lua_error;
        lua_setglobal(L, "config_fn");
    }

    if (string_is_set(p->cfg_file))
        if (luaL_loadfile(L, p->cfg_file) || lua_pcall(L, 0, 0, 0))
            goto lua_error;

    vf->put_image = put_image;
    vf->query_format = query_format;
    vf->config = config;
    vf->uninit = uninit;

    return 1;

lua_error:
    mp_msg(MSGT_VFILTER, MSGL_ERR, "Lua: %s\n", lua_tolstring(L, -1, NULL));
    uninit(vf);
    return 0;
}

#define ST_OFF(f) M_ST_OFF(struct vf_priv_s, f)
static m_option_t vf_opts_fields[] = {
    {"fn", ST_OFF(cfg_fn), CONF_TYPE_STRING, 0, 0, 0, NULL},
    {"fn_l", ST_OFF(cfg_plane_fn[0]), CONF_TYPE_STRING, 0, 0, 0, NULL},
    {"fn_u", ST_OFF(cfg_plane_fn[1]), CONF_TYPE_STRING, 0, 0, 0, NULL},
    {"fn_v", ST_OFF(cfg_plane_fn[2]), CONF_TYPE_STRING, 0, 0, 0, NULL},
    {"file", ST_OFF(cfg_file), CONF_TYPE_STRING, 0, 0, 0, NULL},
    {"rgb", ST_OFF(cfg_rgb), CONF_TYPE_FLAG, 0, 0, 1, NULL},
    {"lut", ST_OFF(cfg_lut), CONF_TYPE_STRING, 0, 0, 0, NULL},
    {"lut_l", ST_OFF(cfg_plane_lut[0]), CONF_TYPE_STRING, 0, 0, 0, NULL},
    {"lut_u", ST_OFF(cfg_plane_lut[1]), CONF_TYPE_STRING, 0, 0, 0, NULL},
    {"lut_v", ST_OFF(cfg_plane_lut[2]), CONF_TYPE_STRING, 0, 0, 0, NULL},
    {"config", ST_OFF(cfg_config), CONF_TYPE_STRING, 0, 0, 0, NULL},
    { NULL, NULL, 0, 0, 0, 0, NULL }
};

static const m_struct_t vf_opts = {
    "dlopen",
    sizeof(struct vf_priv_s),
    &vf_priv_dflt,
    vf_opts_fields
};

const vf_info_t vf_info_lua = {
    "Lua equation/function filter using LuaJIT",
    "lua",
    "wm4",
    "",
    vf_open,
    &vf_opts
};
