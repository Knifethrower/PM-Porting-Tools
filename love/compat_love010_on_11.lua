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
-- and back in 0-255, so getColor -> setColor round trips keep working
local getColor, getBackgroundColor = lg.getColor, lg.getBackgroundColor
lg.getColor = function() local r, g, b, a = getColor() return r*255, g*255, b*255, a*255 end
lg.getBackgroundColor = function() local r, g, b, a = getBackgroundColor() return r*255, g*255, b*255, a*255 end

do -- deprecated in 11 (prints a warning banner), so always replace
	lfs.exists = function(path) return lfs.getInfo(path) ~= nil end
	lfs.isDirectory = function(path) local i = lfs.getInfo(path) return i ~= nil and i.type == "directory" end
	lfs.isFile = function(path) local i = lfs.getInfo(path) return i ~= nil and i.type == "file" end
end

-- Image:getData() is gone in 11: keep each image's ImageData (only for images made from a file
-- name or an ImageData) and give the Image type the method back
do
	local newImage = lg.newImage
	local imagedata = setmetatable({}, {__mode = "k"})
	local patched = false
	lg.newImage = function(src, ...)
		local data = src
		if type(src) == "string" then
			local ok, d = pcall(love.image.newImageData, src)   -- not for compressed (DDS) files
			if ok then data = d end
		end
		local img = newImage(data, ...)
		if type(data) == "userdata" and data.typeOf and data:typeOf("ImageData") then imagedata[img] = data end
		if not patched then
			local mt = getmetatable(img)
			local methods = mt and type(mt.__index) == "table" and mt.__index
			if methods and not methods.getData then
				methods.getData = function(self) return imagedata[self] end
			end
			patched = true
		end
		return img
	end
end

-- 11 needs the source type ("static" was the 0.10 default for files)
local newSource = love.audio.newSource
love.audio.newSource = function(src, kind) return newSource(src, kind or "static") end

-- 11 wants "premultiplied" alpha for multiply, lighten and darken
local setBlendMode = lg.setBlendMode
lg.setBlendMode = function(mode, alphamode)
	if not alphamode and (mode == "multiply" or mode == "lighten" or mode == "darken") then
		alphamode = "premultiplied"
	end
	setBlendMode(mode, alphamode)
end
