-- compat.lua: run this LÖVE 0.10 game on LÖVE 11 (handheld ports): colours were 0-255, are 0-1
-- now, and love.filesystem.exists is gone. Loaded first by main.lua.
local lg, lfs = love.graphics, love.filesystem

local function c255(r, g, b, a)
	if type(r) == "table" then r, g, b, a = r[1], r[2], r[3], r[4] end
	return r/255, g/255, b/255, (a or 255)/255
end

local setColor, setBackgroundColor = lg.setColor, lg.setBackgroundColor
lg.setColor = function(...) setColor(c255(...)) end
lg.setBackgroundColor = function(...) setBackgroundColor(c255(...)) end

do -- deprecated in 11 (prints a warning banner), so always replace
	lfs.exists = function(path) return lfs.getInfo(path) ~= nil end
end

-- 11 wants "premultiplied" alpha for multiply, lighten and darken
local setBlendMode = lg.setBlendMode
lg.setBlendMode = function(mode, alphamode)
	if not alphamode and (mode == "multiply" or mode == "lighten" or mode == "darken") then
		alphamode = "premultiplied"
	end
	setBlendMode(mode, alphamode)
end
