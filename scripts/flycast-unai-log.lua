-- Diagnostic cumulative polygon-span counters; configure from this ELF.
assert(type(bloom_unai_calls_address) == 'number')
assert(type(bloom_unai_pixels_address) == 'number')
assert(type(bloom_unai_output) == 'string')
local exists = io.open(bloom_unai_output, 'r')
if exists then exists:close(); error('Unai output already exists') end
local out = assert(io.open(bloom_unai_output, 'w'))
out:write('host_time,sequence,flags,calls,pixels\n'); out:close()
local previous, last = bloom_test_observer, 0
bloom_test_observer = function()
    if previous then previous() end
    local seq = flycast.memory.read32(bloom_profile_address)
    if seq == 0 or seq % 2 ~= 0 or seq == last then return end
    local duration = flycast.memory.read32(bloom_profile_address + 4)
    local sum = 0
    for i = 0, (bloom_profile_parts or 9) - 1 do
        sum = sum + flycast.memory.read32(bloom_profile_address + 16 + i * 4)
    end
    if duration < 5000000 or duration > 60000000 or sum ~= duration then return end
    -- Cumulative 32-bit counters wrap. Analyze differences modulo 2^32.
    -- These are requested span pixels, not visible/nontransparent pixels.
    local rows = {}
    for flags = 0, 1023 do
        local calls = flycast.memory.read32(bloom_unai_calls_address + flags * 4)
        local pixels = flycast.memory.read32(bloom_unai_pixels_address + flags * 4)
        if calls ~= 0 or pixels ~= 0 then
            rows[#rows + 1] = string.format('%d,%d,%d,%d,%d\n', os.time(), seq, flags, calls, pixels)
        end
    end
    if flycast.memory.read32(bloom_profile_address) ~= seq then return end
    out = assert(io.open(bloom_unai_output, 'a'))
    for _, row in ipairs(rows) do out:write(row) end
    out:close(); last = seq
end
