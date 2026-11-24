-- AC8ThrottleControl 0.1.0
-- Standalone loader, player-address bridge, and independent UMG throttle HUD.
local TAG = '[AC8ThrottleControl] '
local source = debug.getinfo(1, 'S').source
local root = source:match('^@(.*)[/\\]Scripts[/\\][^/\\]+$')
if not root then pcall(print, TAG .. 'disabled: mod folder unavailable\n'); return end

local function read_ini()
    local result, section = {}, ''
    local file = io.open(root .. '\\config.ini', 'r')
    if not file then return result end
    for line in file:lines() do
        local header = line:match('^%s*%[([^%]]+)%]')
        if header then
            section = header:lower()
            result[section] = result[section] or {}
        else
            local key, value = line:match('^%s*([%w_]+)%s*=%s*([^;#]*)')
            if key and result[section] then
                result[section][key:lower()] = value:match('^%s*(.-)%s*$')
            end
        end
    end
    file:close()
    return result
end

local ini = read_ini()
local function bool(section, key, fallback)
    local value = ini[section] and ini[section][key]
    if not value then return fallback end
    value = value:lower()
    if value == 'true' or value == 'yes' or value == 'on' or value == '1' then return true end
    if value == 'false' or value == 'no' or value == 'off' or value == '0' then return false end
    return fallback
end
local function number(section, key, fallback, low, high)
    local value = tonumber(ini[section] and ini[section][key]) or fallback
    return math.max(low, math.min(high, value))
end

if not bool('general', 'enabled', false) then
    pcall(print, TAG .. 'disabled by config.ini\n')
    return
end

local start, failure = package.loadlib(root .. '\\Scripts\\ac8_throttle_control.dll', 'ac8_throttle_start')
if not start then pcall(print, TAG .. 'disabled: native helper not loaded: ' .. tostring(failure) .. '\n'); return end
local ok, err = pcall(start)
if not ok then pcall(print, TAG .. 'disabled: ' .. tostring(err) .. '\n'); return end
if type(LoopInGameThreadAfterFrames) ~= 'function' then
    pcall(print, TAG .. 'disabled: game-thread callback unavailable\n')
    return
end

local hud_enabled = bool('hud', 'enabled', true)
local hud_x = number('hud', 'x', 0.392, 0, 1)
local hud_y = number('hud', 'y', 0.50, 0, 1)
local hud_height = number('hud', 'height', 320, 40, 1000)
local hud_width = number('hud', 'width', 14, 2, 100)
local hud_opacity = number('hud', 'opacity', 1.0, 0.1, 1)
local hud_font = number('hud', 'font_size', 24, 8, 96)
local COLLAPSED, SHOWN, Z = 1, 4, 35
-- Keep peak linear values below 1 so HDR capture does not wash both directions white.
local GREEN = {R=0.02,G=0.55,B=0.05,A=1}
local RED = {R=0.70,G=0.015,B=0.01,A=1}
local AMBER = {R=0.85,G=0.45,B=0.01,A=1}
local WHITE = {R=0.65,G=0.65,B=0.65,A=1}
local RAIL = {R=0.025,G=0.11,B=0.035,A=0.95}
local BACK = {R=0.002,G=0.002,B=0.002,A=0.82}

local function valid(object) return object and object.IsValid and object:IsValid() end
local plane_class, cached_controller, last_address, last_ambiguous
local player_path = root .. '\\player.ptr'

local function current_player()
    if not valid(plane_class) then plane_class = StaticFindObject('/Script/Live.LivePlayerPlane') end
    if not valid(plane_class) then return nil, nil end
    if valid(cached_controller) then
        local pawn = cached_controller.Pawn
        if valid(pawn) and pawn:IsA(plane_class) then return pawn, cached_controller end
    end
    cached_controller = nil
    local selected, owner, count, seen = nil, nil, 0, {}
    for _, controller in ipairs(FindAllOf('LivePlayerController') or {}) do
        if valid(controller) then
            local id = controller.GetAddress and controller:GetAddress() or tostring(controller)
            if not seen[id] then
                seen[id] = true
                local pawn = controller.Pawn
                if valid(pawn) and pawn:IsA(plane_class) then
                    selected, owner, count = pawn, controller, count + 1
                end
            end
        end
    end
    if count == 1 then cached_controller = owner; return selected, owner end
    if count > 1 and count ~= last_ambiguous then
        last_ambiguous = count
        pcall(print, TAG .. count .. ' player controllers own an aircraft; control paused until one remains\n')
    end
    return nil, nil
end

local function write_address(address)
    if address == last_address then return end
    local file = io.open(player_path, 'w')
    if not file then return end
    file:write(string.format('%X\n', address))
    file:close()
    last_address = address
end

local status_file
local function status_checksum(magic, sequence, active, mode, percent, flags)
    return (0xA58C3F19 ~ magic ~ sequence ~ active ~ mode ~ percent ~ flags) & 0xffffffff
end

local function read_status()
    if not status_file then
        status_file = io.open(root .. '\\throttle.status', 'rb')
        if status_file then pcall(function() status_file:setvbuf('no') end) end
    end
    if not status_file then return nil end
    status_file:seek('set', 0)
    local bytes = status_file:read(64)
    if not bytes or #bytes < 64 or not string.unpack then return nil end
    local best
    for offset = 1, 33, 32 do
        local got, magic, sequence, active, mode, percent, flags, checksum = pcall(
            string.unpack, '<I4I4I4I4i4I4I4', bytes, offset)
        if got and magic == 0x31544341 and
           checksum == status_checksum(magic, sequence, active, mode, percent, flags) and
           (not best or sequence > best.sequence) then
            best = {sequence=sequence, active=active~=0, mode=mode,
                    percent=percent/1000, flags=flags}
        end
    end
    return best
end

local ticks = 0
local classes, gauge, hud_failed = {}, {}, false
local game_font, next_font_scan = nil, 0
local function class(path)
    if valid(classes[path]) then return classes[path] end
    classes[path] = StaticFindObject(path)
    return classes[path]
end
local function add(holder, path, z)
    local kind = class(path)
    if not valid(kind) then error('missing ' .. path) end
    local widget = StaticConstructObject(kind, holder)
    if not valid(widget) then error('cannot construct ' .. path) end
    local slot = holder:AddChildToCanvas(widget)
    if not valid(slot) then error('canvas rejected ' .. path) end
    slot:SetZOrder(z)
    widget:SetVisibility(COLLAPSED)
    widget:SetRenderOpacity(hud_opacity)
    return widget, slot
end
local function image_colour(widget, colour)
    widget:SetColorAndOpacity(colour)
    -- Some HUD/HDR paths keep the default white brush tint even after the
    -- UImage colour changes. Apply both multipliers so signed colours survive.
    pcall(function()
        widget:SetBrushTintColor({SpecifiedColor=colour,ColorUseRule=0})
    end)
end
-- After a sortie change a previous gauge widget can still pass IsValid() but
-- no longer be a widget (2026-11-22: "attempt to call a TrivialObject value
-- (method 'SetVisibility')" on every tick, HUD gone for the whole sortie).
-- Any failed call drops the old gauge so the next tick builds a new one.
local function hide_gauge()
    local stale = false
    for _, widget in pairs({gauge.back,gauge.rail,gauge.green,gauge.red,
                            gauge.tick,gauge.value_tick,gauge.text}) do
        if valid(widget) and not pcall(function() widget:SetVisibility(COLLAPSED) end) then
            stale = true
        end
    end
    if stale then gauge = {} end
    gauge.shown = false
end
local function holder_of(controller)
    local hud = valid(controller) and controller.MyHUD
    local canvas = valid(hud) and hud.DynamicCanvas
    local holder = valid(canvas) and canvas:GetParent()
    return valid(holder) and holder or nil
end
local function prepare_gauge(controller)
    local holder = holder_of(controller)
    if not holder then return false end
    if gauge.holder ~= holder then hide_gauge(); gauge = {holder=holder} end
    if valid(gauge.back) and valid(gauge.rail) and valid(gauge.green) and
       valid(gauge.red) and valid(gauge.tick) and valid(gauge.value_tick) and
       valid(gauge.text) then return true end
    local anchors = {Minimum={X=hud_x,Y=hud_y},Maximum={X=hud_x,Y=hud_y}}
    gauge.back,gauge.back_slot = add(holder,'/Script/UMG.Image',Z-3)
    gauge.rail,gauge.rail_slot = add(holder,'/Script/UMG.Image',Z-2)
    gauge.green,gauge.green_slot = add(holder,'/Script/UMG.Image',Z)
    gauge.red,gauge.red_slot = add(holder,'/Script/UMG.Image',Z)
    gauge.tick,gauge.tick_slot = add(holder,'/Script/UMG.Image',Z+1)
    gauge.value_tick,gauge.value_tick_slot = add(holder,'/Script/UMG.Image',Z+2)
    gauge.text,gauge.text_slot = add(holder,'/Script/UMG.TextBlock',Z+2)
    gauge.text_slot:SetAutoSize(true)
    for _, slot in ipairs({gauge.back_slot,gauge.rail_slot,gauge.green_slot,
                           gauge.red_slot,gauge.tick_slot,gauge.value_tick_slot,
                           gauge.text_slot}) do
        slot:SetAnchors(anchors)
    end
    gauge.back_slot:SetAlignment({X=0.5,Y=0.5}); gauge.back_slot:SetPosition({X=0,Y=0})
    gauge.back_slot:SetSize({X=hud_width+10,Y=hud_height+10})
    gauge.rail_slot:SetAlignment({X=0.5,Y=0.5}); gauge.rail_slot:SetPosition({X=0,Y=0})
    gauge.rail_slot:SetSize({X=2,Y=hud_height})
    gauge.green_slot:SetAlignment({X=0.5,Y=1}); gauge.green_slot:SetPosition({X=0,Y=0})
    gauge.red_slot:SetAlignment({X=0.5,Y=0}); gauge.red_slot:SetPosition({X=0,Y=0})
    gauge.tick_slot:SetAlignment({X=0.5,Y=0.5}); gauge.tick_slot:SetPosition({X=0,Y=0})
    gauge.tick_slot:SetSize({X=hud_width*1.8,Y=2})
    gauge.value_tick_slot:SetAlignment({X=0.5,Y=0.5})
    gauge.value_tick_slot:SetPosition({X=0,Y=0})
    gauge.value_tick_slot:SetSize({X=hud_width*2.5,Y=3})
    gauge.text_slot:SetAlignment({X=0,Y=0.5})
    pcall(function() gauge.text:SetJustification(0) end)
    image_colour(gauge.back,BACK); image_colour(gauge.rail,RAIL)
    image_colour(gauge.green,GREEN); image_colour(gauge.red,RED)
    image_colour(gauge.tick,WHITE); image_colour(gauge.value_tick,AMBER)
    return true
end

-- A newly constructed TextBlock has no usable font. Borrow one from the live
-- game HUD, as AC8GuidedWeapons does, and retry at a low rate if it is not yet
-- available during early sortie setup.
local function apply_game_font()
    if gauge.font_ready and valid(gauge.text) then return true end
    if game_font and not valid(game_font.FontObject) then game_font = nil end
    if not game_font and ticks >= next_font_scan then
        next_font_scan = ticks + 30
        local ok, list = pcall(FindAllOf, 'TextBlock')
        if ok and type(list) == 'table' then
            for _, text in ipairs(list) do
                if valid(text) and text ~= gauge.text then
                    local got_name, name = pcall(function() return text:GetFullName() end)
                    local font = text.Font
                    if got_name and name and not name:find('Default__',1,true) and
                       font and valid(font.FontObject) then
                        game_font = {FontObject=font.FontObject,
                                     TypefaceFontName=font.TypefaceFontName}
                        break
                    end
                end
            end
        end
    end
    if not (game_font and valid(game_font.FontObject) and valid(gauge.text)) then return false end
    gauge.text:SetFont({FontObject=game_font.FontObject,
                        TypefaceFontName=game_font.TypefaceFontName,
                        Size=hud_font})
    gauge.font_ready = true
    return true
end
local function text_colour(colour)
    gauge.text:SetColorAndOpacity({SpecifiedColor=colour,ColorUseRule=0})
end
local function show_status(status)
    if not hud_enabled or not status or not status.active or not valid(cached_controller) then hide_gauge(); return end
    local ok = pcall(function()
        if not prepare_gauge(cached_controller) then hide_gauge(); return end
        apply_game_font()
        local percent = math.max(-100, math.min(100, status.percent))
        local brake_hold = (status.flags & 1) ~= 0
        local high_g_yield = (status.flags & 2) ~= 0
        local airbrake = (status.flags & 16) ~= 0
        -- Full brake is shown at the bottom of the scale, not at the stored
        -- value: the air brake, and decelerate held at a brake floor.
        local full_brake = airbrake or (brake_hold and status.mode == 0)
        if full_brake then percent = -100 end
        local green, red
        if status.mode == 0 then
            -- percent: decelerate held at the brake floor is the game's full brake.
            green, red = math.max(0,percent) / 100, brake_hold and 1 or math.max(0,-percent)/100
        else
            green, red = math.max(0,percent)/100, math.max(0,-percent)/100
        end
        gauge.green_slot:SetSize({X=hud_width,Y=hud_height*0.5*green})
        gauge.red_slot:SetSize({X=hud_width,Y=hud_height*0.5*red})
        gauge.green:SetVisibility(green>0 and SHOWN or COLLAPSED)
        gauge.red:SetVisibility(red>0 and SHOWN or COLLAPSED)
        local marker_y = -percent/100*0.5*hud_height
        gauge.value_tick_slot:SetPosition({X=0,Y=marker_y})
        gauge.text_slot:SetPosition({X=hud_width/2+10,Y=marker_y})
        gauge.back:SetVisibility(SHOWN); gauge.rail:SetVisibility(SHOWN)
        gauge.tick:SetVisibility(SHOWN); gauge.value_tick:SetVisibility(SHOWN)
        gauge.text:SetVisibility(SHOWN)
        -- Full brake shows only its label, without a -100% number.
        -- 'AB' could read as afterburner; the air brake shows A.BRK.
        -- The manual-yield flag (4) has no label.
        local words
        if full_brake then
            words = airbrake and 'A.BRK' or 'BRK'
        else
            local whole = percent >= 0 and math.floor(percent+0.5) or math.ceil(percent-0.5)
            words = whole > 0 and string.format('+%d%%',whole) or string.format('%d%%',whole)
            if brake_hold then words = words .. ' BRK' end
        end
        if high_g_yield then words = words .. ' HG' end
        if words ~= gauge.words then gauge.words=words; gauge.text:SetText(FText(words)) end
        text_colour(high_g_yield and AMBER or (red>0 and RED or green>0 and GREEN or WHITE))
        gauge.shown = true
    end)
    if not ok then
        hide_gauge()
        if not hud_failed then
            hud_failed = true
            pcall(print, TAG .. 'HUD unavailable for this sortie; control remains active\n')
        end
    end
end

local generation, last_tick = 0, os.clock()
local function tick()
    last_tick = os.clock()
    ticks = ticks + 1
    if ticks % 10 == 1 then
        local success, pawn, controller = pcall(current_player)
        local address = 0
        if success and pawn then
            cached_controller = controller
            local got, value = pcall(function() return pawn:GetAddress() end)
            if got and type(value) == 'number' then address = value end
        end
        write_address(address)
    end
    if hud_enabled then
        local got, status = pcall(read_status)
        show_status(got and status or nil)
    end
end

-- One recurring registration owns both the 30-frame player poll and the
-- 3-frame HUD refresh. Never register again from inside the callback.
-- UE4SS 3.0.1 can lose a game-thread callback; an async watchdog re-registers
-- one after 5 s without a tick, and a superseded loop ends itself.
local function start_loop()
    generation = generation + 1
    local mine = generation
    LoopInGameThreadAfterFrames(3, function()
        if mine ~= generation then return true end
        tick()
    end)
end
start_loop()
if type(LoopAsync) == 'function' and type(ExecuteInGameThread) == 'function' then
    LoopAsync(1000, function()
        if os.clock() - last_tick > 5 then
            last_tick = os.clock()
            pcall(print, TAG .. 'game-thread loop silent for 5 s; registered again\n')
            ExecuteInGameThread(start_loop)
        end
        return false
    end)
end
pcall(print, TAG .. '0.1.0 loader ready; see AC8ThrottleControl.log\n')
