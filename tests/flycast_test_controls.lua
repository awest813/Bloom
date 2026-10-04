-- Run from the repository root: lua tests/flycast_test_controls.lua
bloom_test_command_file = os.tmpname()
bloom_test_status_file = os.tmpname()
bloom_test_overlay = false
local pressed, released, saved, loaded, paused, resumed, observed = 0, 0, 0, 0, 0, 0, 0
bloom_test_observer = function() observed = observed + 1 end
flycast = {
    input = {
        pressButtons = function(port, mask) assert(port == 1); pressed = mask end,
        releaseButtons = function(port, mask) assert(port == 1); released = mask; pressed = 0 end
    },
    emulator = {
        saveState = function(slot) assert(slot == 9); saved = saved + 1 end,
        loadState = function(slot) assert(slot == 9); loaded = loaded + 1 end,
        pause = function() paused = paused + 1 end,
        resume = function() resumed = resumed + 1 end
    }
}
dofile("scripts/flycast-test-controls.lua")
local function command(text)
    local file = assert(io.open(bloom_test_command_file, "w"))
    file:write(text, "\r\n")
    file:close()
    flycast_callbacks.overlay()
end
command("1 8 2")
assert(pressed == 8 and observed == 1)
flycast_callbacks.vblank()
assert(pressed == 8)
flycast_callbacks.vblank()
assert(pressed == 0 and released == 8)
flycast_callbacks.overlay()
assert(pressed == 0, "Repeated command must not hold again")
command("2 save 9")
flycast_callbacks.overlay()
assert(saved == 0, "Lua state saves can deadlock the vblank callback")
command("3 load 9")
assert(loaded == 0, "Lua state loads can deadlock the vblank callback")
local status = assert(io.open(bloom_test_status_file, "r"))
assert(status:read("*l") == "3 load 9 rejected: use Flycast native state menu")
status:close()
command("4 pause")
command("5 resume")
assert(paused == 0 and resumed == 0, "Overlay must not pause its own emulator")
command("6 2 6000")
for _ = 1, 599 do flycast_callbacks.vblank() end
assert(pressed == 2)
flycast_callbacks.vblank()
assert(pressed == 0, "Hold must be bounded")
command("7 65536 60")
assert(pressed == 0)
local file = assert(io.open(bloom_test_status_file, "r"))
assert(file:read("*l") == "6 2 6000", "Invalid command acknowledged")
file:close()
command("8 2 60")
flycast_callbacks.pause()
assert(pressed == 0)
command("9 2 0")
assert(pressed == 0)
bloom_test_exclusive_input = true
pressed = 8 -- unrelated host input in the isolated instance
flycast_callbacks.overlay()
assert(pressed == 0)
command("10 2 2")
flycast_callbacks.vblank()
assert(pressed == 2, "Isolation must preserve the scripted hold")
flycast_callbacks.vblank()
assert(pressed == 0)
-- A frozen/black guest may never invoke overlay again. Controller commands
-- must still be accepted and released from the vblank path.
local file = assert(io.open(bloom_test_command_file, "w"))
file:write("11 4 3\n"); file:close()
flycast_callbacks.vblank()
assert(pressed == 4)
flycast_callbacks.vblank()
assert(pressed == 4)
flycast_callbacks.vblank()
assert(pressed == 0)
flycast_callbacks.overlay()
assert(pressed == 0, "Overlay must not replay a vblank command")
file = assert(io.open(bloom_test_command_file, "w"))
file:write("12 save 9\n"); file:close()
flycast_callbacks.vblank()
assert(saved == 0, "State operations must stay out of the vblank callback")
flycast_callbacks.overlay()
assert(saved == 0, "State operations must stay out of the overlay callback")
os.remove(bloom_test_command_file)
os.remove(bloom_test_status_file)
print("Flycast controller checks passed")
