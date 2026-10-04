-- Low-overhead counter capture; use this ELF's bloom_frame_sample address.
assert(type(bloom_frame_address) == 'number', 'Missing frame snapshot address')
assert(type(bloom_frame_output) == 'string', 'Missing output path')
local existing = io.open(bloom_frame_output, 'r')
if existing then existing:close(); error('Output already exists') end
local out = assert(io.open(bloom_frame_output, 'w'))
out:write('host_time,sequence,duration_ms,presentations,vblanks,skip_decisions,total_decisions\n')
out:close()
local previous, last = bloom_test_observer, 0
bloom_test_observer = function()
    if previous then previous() end
    local seq = flycast.memory.read32(bloom_frame_address)
    if seq == 0 or seq % 2 ~= 0 or seq == last then return end
    local words = {}
    for i = 1, 5 do words[i] = flycast.memory.read32(bloom_frame_address + i * 4) end
    if flycast.memory.read32(bloom_frame_address) ~= seq then return end
    if words[1] < 1000 or words[1] > 60000 or words[2] == 0 then return end
    local row = assert(io.open(bloom_frame_output, 'a'))
    row:write(tostring(os.time()), ',', tostring(seq))
    for _, word in ipairs(words) do row:write(',', tostring(word)) end
    row:write('\n'); row:close(); last = seq
end
