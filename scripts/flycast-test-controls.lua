-- Optional Flycast test overlay. Copy beside emu.cfg as flycast.lua.
-- Uses the public Flycast Lua controller/UI API; no guest memory is changed.
-- https://github.com/flyinghead/flycast/blob/master/core/lua/flycast.lua
local held, remaining = 0, 0
local command_file = bloom_test_command_file or "bloom-input.txt"
local last_command = ""
local function acknowledge(command)
    if not bloom_test_status_file then return end
    local file = io.open(bloom_test_status_file, "w")
    if file then
        file:write(command, "\n")
        file:close()
    end
end
local function release()
    if bloom_test_exclusive_input then
        flycast.input.releaseButtons(1, 65535)
    elseif held ~= 0 then
        flycast.input.releaseButtons(1, held)
    end
    held, remaining = 0, 0
end
local function press(mask, frames)
    release()
    if mask == 0 or (frames and frames <= 0) then return end
    held, remaining = mask, math.min(frames or 60, 600)
    flycast.input.pressButtons(1, held)
end
-- Controller commands must work even if the guest stops producing pictures.
-- State operations use Flycast's native menu. Its Lua save/load bindings
-- stop the emulation thread while holding the Lua mutex; that thread can
-- already be waiting for this mutex to deliver our vblank callback.
local function poll_command()
    local file = io.open(command_file, "r")
    if file then
        local command = (file:read("*l") or ""):gsub("\r$", "")
        file:close()
        if command ~= last_command then
            local sequence, mask, duration = command:match("^(%d+) (%d+) (%d+)$")
            if sequence and tonumber(mask) <= 65535 then
                last_command = command
                press(tonumber(mask), math.min(tonumber(duration), 600))
                acknowledge(command)
            elseif command:match("^%d+ save 9$") or command:match("^%d+ load 9$") then
                last_command = command
                release()
                acknowledge(command .. " rejected: use Flycast native state menu")
            -- Do not call pause/resume from the overlay callback:
            -- pause can wait for this callback and deadlock Flycast.
            end
        end
    end
end
local buttons = {
    {"Start", 8}, {"Up", 16}, {"Down", 32},
    {"Left", 64}, {"Right", 128},
    {"Circle / Confirm", 2}, {"Cross / Cancel", 4},
    {"Triangle", 512}, {"Square", 1024}
}
flycast_callbacks = {
    start = release,
    loadState = release,
    pause = release,
    terminate = release,
    vblank = function()
        poll_command()
        -- Black-screen boot failures may never render an overlay. Keep
        -- diagnostic sampling alive while the Dreamcast CPU still runs.
        if bloom_test_observer then bloom_test_observer() end
        -- Opt in only in a separate benchmark instance. This prevents host
        -- keyboard/controller activity from changing the scripted scene.
        if bloom_test_exclusive_input then flycast.input.releaseButtons(1, 65535) end
        if remaining > 0 then
            flycast.input.pressButtons(1, held)
            remaining = remaining - 1
            if remaining == 0 then release() end
        end
    end,
    overlay = function()
        if bloom_test_exclusive_input then
            flycast.input.releaseButtons(1, 65535)
            if remaining > 0 then flycast.input.pressButtons(1, held) end
        end
        if bloom_test_observer then bloom_test_observer() end
        local ui = flycast.ui
        -- A sidecar supports repeatable controller tests when host input taps
        -- are too brief. Format: unique-sequence button-mask hold-vblanks.
        poll_command()
        if bloom_test_overlay == false then return end
        ui.beginWindow("Bloom test controls", 8, 8, 170, 0)
        ui.text("Input: " .. held .. " / " .. remaining)
        for _, button in ipairs(buttons) do
            local mask = button[2]
            ui.button(button[1], function() press(mask) end)
        end
        ui.button("Release", release)
        ui.text("Save/load states through Flycast's native menu")
        ui.endWindow()
    end
}
