-- This script is loaded by vf_lua.

MAX_PLANES = 3

-- Prefixed to the user's expressions to create pixel functions.
-- Basically define what arguments are passed to the function directly.
PIXEL_FN_PRELUDE = "local x, y, fx, fy, c = ...; return "

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
        for i = 1, img.plane_count do
            img[i].scale_x = img[i].width / img.width
            img[i].scale_y = img[i].height / img.height
            -- Can't do this type conversion with the C API.
            -- Also, doing the cast in an inner loop makes it slow.
            img[i].ptr = ffi.cast('uint8_t*', img[i].ptr)
        end
    end
    prepare_image(src)
    prepare_image(dst)
end

function plane_rowptr(plane, y)
    return ffi.cast(plane.pixel_ptr_type, plane.ptr + plane.stride * y)
end

local function _px_noclip(plane, x, y)
    return plane_rowptr(plane, y)[x] / plane.max
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
            local res = pixel_fn(x, y, fx, fy, src_ptr[x] / src.max)
            res = math.max(math.min(res, 1), 0)
            dst_ptr[x] = res * dst.max
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
