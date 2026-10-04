-- Load before flycast-test-controls.lua in a diagnostic-only test instance.
-- Set bloom_profile_address from THIS ELF's bloom_profile_sample symbol,
-- and bloom_profile_output to a new CSV path. Reads guest memory only.
assert(type(bloom_profile_address) == "number", "Missing ELF snapshot address")
assert(type(bloom_profile_output) == "string", "Missing profile CSV path")
local address, output = bloom_profile_address, bloom_profile_output
local previous_observer = bloom_test_observer
local last_sequence = 0
local names = {"other", "ps1", "draw", "sound", "video", "mdec", "disc",
               "interpreter", "geometry"}
-- Older diagnostic binaries have nine slots. Select ten explicitly for
-- builds with hardware I/O attribution; never infer a layout from RAM.
local parts = bloom_profile_parts or 9
assert(parts == 9 or parts == 10, "Unsupported profile layout")
if parts == 10 then names[#names + 1] = "io" end
local existing = io.open(output, "r")
if existing then
    existing:close()
    error("Profile output already exists: " .. output)
end
local file = assert(io.open(output, "w"))
file:write("host_time,sequence,duration_us,vblanks,presentations")
for _, name in ipairs(names) do file:write(",", name, "_us") end
for _, name in ipairs(names) do file:write(",", name, "_calls") end
file:write("\n")
file:close()

bloom_test_observer = function()
    if previous_observer then previous_observer() end
    local sequence = flycast.memory.read32(address)
    if sequence == 0 or sequence % 2 ~= 0 or sequence == last_sequence then return end
    local words = {}
    for i = 1, 3 + 2 * parts do words[i] = flycast.memory.read32(address + i * 4) end
    if flycast.memory.read32(address) ~= sequence then return end
    -- Before Bloom has booted, this address can contain unrelated data.
    local duration, sum = words[1], 0
    for i = 4, 3 + parts do sum = sum + words[i] end
    if duration < 5000000 or duration > 60000000 or sum ~= duration then return end
    local row = assert(io.open(output, "a"))
    row:write(tostring(os.time()), ",", tostring(sequence))
    for _, word in ipairs(words) do row:write(",", tostring(word)) end
    row:write("\n")
    row:close()
    last_sequence = sequence
end
