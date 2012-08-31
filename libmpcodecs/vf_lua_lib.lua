-- This script is loaded by vf_lua.
-- For copyright information see vf_lua.c.

MAX_PLANES = 3

-- Prefixed to the user's expressions to create pixel functions.
-- Basically define what arguments are passed to the function directly.
PIXEL_FN_PRELUDE = "local x, y, r, g, b = ...; local c = r ; return "
CONFIG_FN_PRELUDE = "local width, height = ... ; return "
LUT_FN_PRELUDE = "local c = ... ; return "

local ffi = require('ffi')

--LuaJIT debugging hooks - useful for figuring out why something is slow.
--Needs http://repo.or.cz/w/luajit-2.0.git/blob/HEAD:/src/jit/v.lua
--require('v').start('yourlogdump.txt')

-- These are filled by the C code (and _prepare_filter) by default.
dst = {}
src = {}

for n = 1, MAX_PLANES do
    dst[n] = {}
    src[n] = {}
end

-- Called by the C code, after it has set the fields in _G.src and _G.dst.
function _prepare_filter()
    local function prepare_image(img)
        local px_pack, px_unpack
        if img[1].packed_rgb then
            px_pack, px_unpack = _px_pack_rgb32, _px_unpack_rgb32
        else
            px_pack, px_unpack = _px_pack_planar, _px_unpack_planar
        end
        for i = 1, img.plane_count do
            img[i].scale_x = img[i].width / img.width
            img[i].scale_y = img[i].height / img.height
            img[i].inv_max = 1 / img[i].max
            -- Can't do this type conversion with the C API.
            -- Also, doing the cast in an inner loop makes it slow.
            img[i].ptr = ffi.cast('uint8_t*', img[i].ptr)
            img[i]._px_pack = px_pack
            img[i]._px_unpack = px_unpack
        end
    end
    prepare_image(src)
    prepare_image(dst)
end

function _px_pack_rgb32(plane, r, g, b)
    r = math.max(math.min(r, 1), 0) * plane.max
    g = math.max(math.min(g, 1), 0) * plane.max
    b = math.max(math.min(b, 1), 0) * plane.max
    g = bit.lshift(g, 8)
    b = bit.lshift(b, 16)
    return bit.bor(r, bit.bor(g, b))
end

function _px_unpack_rgb32(plane, raw)
    local r = bit.band(raw, 0xFF)
    local g = bit.band(bit.rshift(raw, 8), 0xFF)
    local b = bit.band(bit.rshift(raw, 16), 0xFF)
    r = r * plane.inv_max
    g = g * plane.inv_max
    b = b * plane.inv_max
    return r, g, b
end

function _px_pack_planar(plane, c)
    return math.max(math.min(c, 1), 0) * plane.max
end

function _px_unpack_planar(plane, c)
    return c * plane.inv_max
end

function plane_rowptr(plane, y)
    return ffi.cast(plane.pixel_ptr_type, plane.ptr + plane.stride * y)
end

local function _px_noclip(plane, x, y)
    return plane:_px_unpack(plane_rowptr(plane, y)[x])
end

function plane_clip(plane, x, y)
    x = math.max(math.min(x, plane.width - 1), 0)
    y = math.max(math.min(y, plane.height - 1), 0)
    return x, y
end

function plane_px(plane, x, y)
    x, y = plane_clip(plane, x, y)
    return _px_noclip(plane, x, y)
end

function px(plane_nr, x, y)
    return plane_px(src[plane_nr], x, y)
end

-- Setup the environment for the pixel function, i.e. things that the pixel
-- function can access, without dragging it around as function parameter.
local function _setup_fenv(dst, src, pixel_fn)
    -- Re-using the global environment might be slightly unclean, but is
    -- simple - one consequence is that LuaJIT might have to recompile the
    -- inner loop that follows.
    local fn_env = _G
    fn_env.p = function(x, y) return px(src.plane_nr, x, y) end
    fn_env.pw = src.width
    fn_env.ph = src.height
    fn_env.sw = src.scale_x
    fn_env.sh = src.scale_y
end

-- This is a micro-optimization: passing "c" instead of calling "p(x,y)" on
-- each pixel is slightly faster. It's probably not very important; remove it
-- if it's annoying, and use _filter_plane() instead.
function _filter_plane_noscale(dst, src, pixel_fn)
    _setup_fenv(dst, src, pixel_fn)

    for y = 0, dst.height - 1 do
        local src_ptr = ffi.cast(src.pixel_ptr_type, src.ptr + src.stride * y)
        local dst_ptr = ffi.cast(dst.pixel_ptr_type, dst.ptr + dst.stride * y)
        for x = 0, dst.width - 1 do
            dst_ptr[x] = dst:_px_pack(pixel_fn(x, y, src:_px_unpack(src_ptr[x])))
        end
    end
end

function _filter_plane(dst, src, pixel_fn)
    _setup_fenv(dst, src, pixel_fn)

    for y = 0, dst.height - 1 do
        local dst_ptr = ffi.cast(dst.pixel_ptr_type, dst.ptr + dst.stride * y)
        for x = 0, dst.width - 1 do
            local r, g, b = plane_px(src, x, y)
            dst_ptr[x] = dst:_px_pack(pixel_fn(x, y, r, g, b))
        end
    end
end

local function _lut_filter_plane(dst, src, lut)
    for y = 0, src.height - 1 do
        local src_ptr = ffi.cast(src.pixel_ptr_type, src.ptr + src.stride * y)
        local dst_ptr = ffi.cast(dst.pixel_ptr_type, dst.ptr + dst.stride * y)
        for x = 0, src.width - 1 do
            dst_ptr[x] = lut[src_ptr[x]]
        end
    end
end

function copy_plane(dst, src)
    for y = 0, src.height - 1 do
        ffi.copy(plane_rowptr(dst, y), plane_rowptr(src, y),
                 src.bytes_per_pixel * src.width)
    end
end

local function _get_lut(dst, src, i)
    if not plane_lut then
        _G.plane_lut = {}
    end
    if not plane_lut[i] then
        assert(src[1].imgfmt == dst[1].imgfmt,
               "lut requires src format = dst format")
        assert(src.width == dst.width and src.height == dst.height,
              "lut requires equal image dimensions for src and dst")
        plane_lut[i] = generate_lut(src[1], plane_lut_fn[i])
    end
    return plane_lut[i]
end

-- This is called each time a new image is to be filtered.
function filter_image()
    for i = 1, src.plane_count do
        local psrc, pdst = src[i], dst[i]
        if plane_fn[i] then
            if psrc.width == pdst.width and psrc.height == pdst.height then
                _filter_plane_noscale(pdst, psrc, plane_fn[i])
            else
                _filter_plane(pdst, psrc, plane_fn[i])
            end
        elseif plane_lut_fn[i] then
            _lut_filter_plane(pdst, psrc, _get_lut(dst, src, i))
        else
            copy_plane(pdst, psrc)
        end
    end
end

-- fn: lookup function, works in the range [0, 1]
function generate_lut(plane, fn)
    assert(not plane.packed_rgb, "lut doesn't work on packed RGB")

    local bpp = plane.bytes_per_pixel
    local max = plane.max

    local type
    if bpp == 1 then
        type = "uint8_t"
    elseif bpp == 2 then
        type = "uint16_t"
    else
        error("unsupported LUT size: " .. bpp)
    end
    local lut_size = bit.lshift(1, ffi.sizeof(type) * 8)
    assert(max >= 0 and max < lut_size)
    local lut = ffi.new(type .. "[?]", lut_size)
    for n = 0, max do
        lut[n] = plane:_px_pack(fn(plane:_px_unpack(n)))
    end
    return lut
end

-- The is called on each config() call.
function config(width, height, d_width, d_height)
    -- invalidate previous things
    _G.lut = nil

    if config_fn then
        width, height = config_fn(width, height)
        d_width = width
        d_height = height
    end

    return width, height, d_width, d_height
end
