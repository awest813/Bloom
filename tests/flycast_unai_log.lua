local output = os.tmpname(); os.remove(output)
bloom_profile_address = 0x1000
bloom_profile_parts = 10
bloom_unai_calls_address = 0x2000
bloom_unai_pixels_address = 0x4000
bloom_unai_output = output
local sequence, duration, total, calls, pixels = 2, 5000000, 5000000, 12, 240
local sequence_reads, torn, chained = 0, false, 0
bloom_test_observer = function() chained = chained + 1 end
flycast = {memory = {read32 = function(address)
    if address == 0x1000 then
        sequence_reads = sequence_reads + 1
        return torn and sequence_reads > 1 and sequence + 2 or sequence
    end
    if address == 0x1004 then return duration end
    if address == 0x1010 then return total end
    if address == 0x2000 + 33*4 then return calls end
    if address == 0x4000 + 33*4 then return pixels end
    return 0
end}}
local function contents()
    local f = assert(io.open(output)); local s = f:read('*a'); f:close(); return s
end
local function observe() sequence_reads = 0; bloom_test_observer() end
dofile('scripts/flycast-unai-log.lua')
observe(); local first = contents()
assert(first:find(',2,33,12,240\n', 1, true)); assert(chained == 1)
observe(); assert(contents() == first)
sequence = 3; observe(); assert(contents() == first)
sequence = 4; torn = true; observe(); assert(contents() == first)
torn = false; duration = 1; observe(); assert(contents() == first)
duration = 5000000; total = 1; observe(); assert(contents() == first)
total = duration; calls = 4294967295; pixels = 0
observe(); local final = contents()
assert(final:find(',4,33,4294967295,0\n', 1, true))
assert(not pcall(dofile, 'scripts/flycast-unai-log.lua')); assert(contents() == final)
os.remove(output)
print('Unai polygon counter logger checks passed')
