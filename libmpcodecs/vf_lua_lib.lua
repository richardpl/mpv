-- This script is loaded by vf_lua.

MAX_PLANES = 3

-- Prefixed to the user's expressions to create pixel functions.
-- Basically define what arguments are passed to the function directly.
PIXEL_FN_PRELUDE = "local x, y, fx, fy, r, g, b = ...; local c = r ; return "

local ffi = require('ffi')

--LuaJIT debugging hooks - useful for figuring out why something is slow.
--Needs http://repo.or.cz/w/luajit-2.0.git/blob/HEAD:/src/jit/v.lua
--require('v').start('yourlogdump.txt')

-- These are filled by the C code (and _prepare_filter) by default.
width = 0
height = 0
plane_count = 0
dst = {}
src = {}

px_fns = {}
for n = 1, MAX_PLANES do
    dst[n] = {}
    src[n] = {}
end

function _prepare_filter()
    _G.width = src.width
    _G.height = src.height
    local function prepare_image(img)
        local px_pack, px_unpack
        if img.packed_rgb then
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

function px(plane_nr, x, y)
    local plane = src[plane_nr]
    x, y = plane_clip(plane, x, y)
    return _px_noclip(plane, x, y)
end

function pxf(plane_nr, x, y)
    local plane = src[plane_nr]
    x = x * plane.width
    y = y * plane.height
    x, y = plane_clip(plane, x, y)
    return _px_noclip(plane, x, y)
end

function _filter_plane(dst, src, pixel_fn)
    -- Setup the environment for the pixel function (re-using the global
    -- environment might be slightly unclean, but is simple - one consequence
    -- is that LuaJIT will only JIT the following inner loop).
    _G.p = function(x, y) return px(src.plane_nr, x, y) end
    _G.pf = function(x, y) return pxf(src.plane_nr, x, y) end
    _G.pw = src.width
    _G.ph = src.height
    _G.sw = src.scale_x
    _G.sh = src.scale_y

    for y = 0, src.height - 1 do
        local src_ptr = ffi.cast(src.pixel_ptr_type, src.ptr + src.stride * y)
        local dst_ptr = ffi.cast(dst.pixel_ptr_type, dst.ptr + dst.stride * y)
        local fy = y / src.height
        for x = 0, src.width - 1 do
            local fx = x / src.width
            dst_ptr[x] = dst:_px_pack(pixel_fn(x, y, fx, fy,
                                               src:_px_unpack(src_ptr[x])))
        end
    end
end

function copy_plane(dst, src)
    for y = 0, src.height - 1 do
        ffi.copy(plane_rowptr(dst, y), plane_rowptr(src, y),
                 src.bytes_per_pixel * src.width)
    end
end

function filter_image()
    for i = 1, src.plane_count do
        if plane_fn and plane_fn[i] then
            _filter_plane(dst[i], src[i], plane_fn[i])
        else
            copy_plane(dst[i], src[i])
        end
    end
end
