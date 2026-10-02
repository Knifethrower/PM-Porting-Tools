--[[ fit.lua: fill a handheld's screen with a LÖVE 11 game made for a fixed window size.

The game keeps believing its window is W x H; the real window is fullscreen, the picture is scaled
uniformly (optionally by whole numbers) and centred with black bars, and mouse/touch coordinates
are mapped back into the game's space. One line at the top of main.lua, no other game edits:

    require("fit").setup(800, 600)                    -- active when the launcher exports PM_FIT=1
                                                      -- (before any compat shim: it keeps the originals)
    require("fit").setup(256, 192, {integer = true, pointer = true})

W x H is the size the game draws at (its window size at the scale it picks by default). Games
that offer their own window-size option should keep it at that default.

options:  env      name of the switch variable (default "PM_FIT"); always = true ignores it
          integer  whole-number scale only (pixel art); fractional when the screen is smaller
          filter   "nearest" or "linear" for the final scaling (default: nearest if integer, else linear)
          pointer  draw an arrow at the mouse (KMSDRM has no hardware cursor; gptokeyb mouse)
          bars     "game": fill the bars with the game's background colour (default black)
          vsync    passed to the fullscreen mode (default: keep the game's)

How: everything the game draws "to the screen" goes into a W x H canvas instead (setCanvas()
with no canvas selects it; love.run's origin() at the start of each frame selects it again);
present() draws that canvas scaled and centred, then the pointer, and leaves no canvas active.
The game's transforms, scissor and stencil work unchanged in its own coordinates, and shaders
see love_ScreenSize = W x H. setMode/updateMode/setFullscreen keep the fullscreen window,
getDimensions/getWidth/getHeight/getMode/getDesktopDimensions report W x H (so games that pick
their own window scale from the desktop size stay at 1x), the mouse functions and the mouse/touch
handlers convert coordinates. Cost: one W x H canvas and one extra full-screen draw per frame.

From the Orthorobot, Trick Parade, Safety Blanket, TROSH, IYFCT, Sienna and Duck Marines ports,
which each had this hand-written. License: 0BSD. ]]

local fit = {active = false, w = 0, h = 0, scale = 1, x = 0, y = 0}

local lg, lw, lm = love.graphics, love.window, love.mouse
local real = {}
local opts = {}
local screen            -- the W x H canvas that stands in for the window
local on_screen = true  -- the game is drawing to its "window" (our canvas)
local pending = true    -- presented: select the canvas again at the next frame's origin()

local function layout()
	local W, H = real.getDimensions()
	local s = math.min(W / fit.w, H / fit.h)
	if opts.integer and s >= 1 then s = math.floor(s) end
	fit.scale = s
	fit.x = math.floor((W - fit.w * s) / 2)
	fit.y = math.floor((H - fit.h * s) / 2)
end

-- game coordinates <-> window pixels
function fit.to_game(x, y) return (x - fit.x) / fit.scale, (y - fit.y) / fit.scale end
function fit.to_screen(x, y) return x * fit.scale + fit.x, y * fit.scale + fit.y end

local function select_screen() real.setCanvas({screen, stencil = true}) end

local function draw_pointer()
	local x, y = real.getPosition()
	real.setColor(0, 0, 0, 1)
	real.polygon("fill", x - 2, y - 3, x + 17, y + 14, x + 7, y + 15, x + 1, y + 23)
	real.setColor(1, 1, 1, 1)
	real.polygon("fill", x, y, x + 13, y + 12, x + 6, y + 12, x + 1, y + 19)
end

local function wrap_handler(name, convert)
	local h = love.handlers[name]
	if h then love.handlers[name] = function(...) return h(convert(...)) end end
end

function fit.setup(w, h, o)
	opts = o or {}
	fit.w, fit.h = w, h
	if not (opts.always or os.getenv(opts.env or "PM_FIT")) then return fit end
	fit.active = true

	-- the originals, taken now: compatibility shims required later (0-255 setColor/clear of the
	-- LÖVE 0.10 shim) must not reach present()'s own drawing. Require fit before them.
	for _, k in ipairs({"present", "origin", "setCanvas", "getCanvas", "getDimensions", "getWidth", "getHeight",
	                    "push", "pop", "setScissor", "setShader", "setBlendMode", "clear", "setColor", "draw", "polygon",
                    "getBackgroundColor"}) do
		real[k] = lg[k]
	end
	for _, k in ipairs({"setMode", "updateMode", "setFullscreen", "getMode"}) do real[k] = lw[k] end
	if lm then                    -- games can switch the mouse module off in conf.lua
		for _, k in ipairs({"getPosition", "getX", "getY", "setPosition"}) do real[k] = lm[k] end
	end

	local _, _, flags = lw.getMode()
	flags.fullscreen, flags.fullscreentype = true, "desktop"
	flags.x, flags.y = nil, nil   -- getMode() reports the windowed position; kept, it offsets the fullscreen window
	if opts.vsync ~= nil then flags.vsync = opts.vsync end
	real.setMode(0, 0, flags)
	layout()
	if opts.pointer and lm then lm.setVisible(false) end

	screen = lg.newCanvas(w, h)
	local filter = opts.filter or (opts.integer and "nearest" or "linear")
	screen:setFilter(filter, filter)

	lg.setCanvas = function(...)
		if select("#", ...) == 0 or (...) == nil then
			on_screen = true
			return select_screen()
		end
		on_screen = false
		return real.setCanvas(...)
	end
	lg.getCanvas = function(...)
		if on_screen then return nil end
		return real.getCanvas(...)
	end
	lg.present = function(...)
		real.push("all")
		real.setCanvas()
		real.origin()
		real.setScissor()
		real.setShader()
		real.setBlendMode("alpha", "premultiplied")
		if opts.bars == "game" then real.clear(real.getBackgroundColor()) else real.clear(0, 0, 0, 1) end
		real.setColor(1, 1, 1, 1)
		real.draw(screen, fit.x, fit.y, 0, fit.scale)
		real.setBlendMode("alpha")
		if opts.pointer and lm then draw_pointer() end
		real.pop()                  -- restores the game's state, including its canvas...
		real.setCanvas()          -- ...but present() needs none active
		real.present(...)
		pending = true            -- LÖVE refuses event.pump() while a canvas is active
	end
	lg.origin = function()
		real.origin()
		if pending then
			pending = false
			if on_screen then select_screen() end
		end
	end
	lg.getDimensions = function() return fit.w, fit.h end
	lg.getWidth = function() return fit.w end
	lg.getHeight = function() return fit.h end

	lw.setMode = function() layout(); return true end
	lw.updateMode = lw.setMode
	lw.setFullscreen = function() return true end
	lw.getMode = function() local _, _, f = real.getMode(); return fit.w, fit.h, f end
	lw.getDesktopDimensions = function() return fit.w, fit.h end

	if lm then
		lm.getPosition = function() return fit.to_game(real.getPosition()) end
		lm.getX = function() return (real.getX() - fit.x) / fit.scale end
		lm.getY = function() return (real.getY() - fit.y) / fit.scale end
		lm.setPosition = function(x, y) real.setPosition(fit.to_screen(x, y)) end
	end

	local function pos(x, y, ...) local gx, gy = fit.to_game(x, y); return gx, gy, ... end
	wrap_handler("mousepressed", pos)
	wrap_handler("mousereleased", pos)
	wrap_handler("mousemoved", function(x, y, dx, dy, ...)
		local gx, gy = fit.to_game(x, y)
		return gx, gy, dx / fit.scale, dy / fit.scale, ...
	end)
	local function touch(id, x, y, dx, dy, ...)
		local gx, gy = fit.to_game(x, y)
		return id, gx, gy, dx / fit.scale, dy / fit.scale, ...
	end
	wrap_handler("touchpressed", touch)
	wrap_handler("touchreleased", touch)
	wrap_handler("touchmoved", touch)
	wrap_handler("resize", function() layout(); return fit.w, fit.h end)

	return fit
end

-- for games with their own love.run that never call love.graphics.origin(): call at the start of each frame
function fit.frame() if fit.active then love.graphics.origin() end end

return fit
