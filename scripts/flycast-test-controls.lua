-- Optional Flycast test overlay. Copy beside emu.cfg as flycast.lua.
-- Uses the public Flycast Lua controller/UI API; no guest memory is changed.
-- https://github.com/flyinghead/flycast/blob/master/core/lua/flycast.lua
local held, remaining = 0, 0
local command_file = bloom_test_command_file or "bloom-input.txt"
local last_command = ""
local function release()
    if held ~= 0 then flycast.input.releaseButtons(1, held) end
    held, remaining = 0, 0
end
local function press(mask, frames)
    release()
    if mask == 0 or (frames and frames <= 0) then return end
    held, remaining = mask, math.min(frames or 60, 600)
    flycast.input.pressButtons(1, held)
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
        if remaining > 0 then
            flycast.input.pressButtons(1, held)
            remaining = remaining - 1
            if remaining == 0 then release() end
        end
    end,
    overlay = function()
        local ui = flycast.ui
        -- A sidecar supports repeatable controller tests when host input taps
        -- are too brief. Format: unique-sequence button-mask hold-vblanks.
        local file = io.open(command_file, "r")
        if file then
            local command = file:read("*l") or ""
            file:close()
            if command ~= last_command then
                local sequence, mask, duration = command:match("^(%d+) (%d+) (%d+)$")
                if sequence and tonumber(mask) <= 65535 then
                    last_command = command
                    press(tonumber(mask), math.min(tonumber(duration), 600))
                elseif command:match("^%d+ save 9$") then
                    -- Explicit checkpoint command; reserve slot 9 for tests.
                    last_command = command
                    release()
                    flycast.emulator.saveState(9)
                end
            end
        end
        if bloom_test_overlay == false then return end
        ui.beginWindow("Bloom test controls", 8, 8, 170, 0)
        ui.text("Input: " .. held .. " / " .. remaining)
        for _, button in ipairs(buttons) do
            local mask = button[2]
            ui.button(button[1], function() press(mask) end)
        end
        ui.button("Release", release)
        ui.button("Save test state", function() flycast.emulator.saveState(9) end)
        ui.endWindow()
    end
}
