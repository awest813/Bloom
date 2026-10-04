local output = os.tmpname(); os.remove(output)
bloom_frame_address, bloom_frame_output = 4096, output
local words = {2, 1000, 19, 30, 1, 4}
local reads, torn = 0, false
flycast = {memory = {read32 = function(address)
    reads = reads + 1
    if torn and reads == 7 then return 8 end
    return words[(address - 4096) / 4 + 1]
end}}
local function contents()
    local f = assert(io.open(output)); local text = f:read('*a'); f:close(); return text
end
local function observe() reads = 0; bloom_test_observer() end
dofile('scripts/flycast-frame-log.lua')
observe(); local first = contents()
assert(first:find(',2,1000,19,30,1,4\n', 1, true))
observe(); assert(contents() == first)
words[1] = 3; observe(); assert(contents() == first)
words[1] = 4; torn = true; observe(); assert(contents() == first)
torn = false; observe(); local final = contents(); assert(#final > #first)
assert(not pcall(dofile, 'scripts/flycast-frame-log.lua'))
assert(contents() == final)
os.remove(output)
print('Low-overhead frame logger checks passed')
