--  arith.lua -- THE MESH'S OWN ARITHMETIC, reached without leaving the
--  compiled script.
--
--  arc.put.f32 narrows a number to a float as the mesh's arithmetic
--  does, and arc.put.sqrt, sin, cos, tan, acos and atan2 are the float
--  functions it calls (src/script/api_put.c).  Each is a C function, and
--  on LuaJIT a call to one leaves the compiled script and comes back:
--  the geometry calls them millions of times a build.  So on LuaJIT the
--  same arithmetic is reached through the FFI.  A float stored and read
--  back is the C cast, and the functions are the same functions of the
--  same library, so every answer is the C function's to the bit.  On
--  Lua 5.4 there is no FFI, and the C functions stand.

if not jit then return end

local ffi = require "ffi"
ffi.cdef [[
float sqrtf(float);
float sinf(float);
float cosf(float);
float tanf(float);
float acosf(float);
float atan2f(float, float);
]]
local C = ffi.C

local cell = ffi.new("float[1]")
arc.put.f32 = function (x)
    cell[0] = x
    return cell[0]
end
arc.put.sqrt  = function (x) return C.sqrtf(x) end
arc.put.sin   = function (x) return C.sinf(x) end
arc.put.cos   = function (x) return C.cosf(x) end
arc.put.tan   = function (x) return C.tanf(x) end
arc.put.acos  = function (x) return C.acosf(x) end
arc.put.atan2 = function (y, x) return C.atan2f(y, x) end
