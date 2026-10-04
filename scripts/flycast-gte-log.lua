-- Diagnostic-only per-operation GTE counters. Use this ELF's symbol address.
assert(type(bloom_gte_address) == 'number', 'Missing GTE snapshot address')
assert(type(bloom_gte_output) == 'string', 'Missing GTE output path')
local names = {'rtps','nclip','op','dpcs','intpl','mvmva','ncds','cdp',
               'ncdt','nccs','cc','ncs','nct','sqr','dcpl','dpct',
               'avsz3','avsz4','rtpt','gpf','gpl','ncct'}
local existing = io.open(bloom_gte_output, 'r')
if existing then existing:close(); error('Output already exists') end
local out = assert(io.open(bloom_gte_output, 'w'))
out:write('host_time,sequence,duration_us')
for _, suffix in ipairs({'_us','_calls'}) do
    for _, name in ipairs(names) do out:write(',', name, suffix) end
end
out:write('\n'); out:close()
local previous, last = bloom_test_observer, 0
bloom_test_observer = function()
    if previous then previous() end
    local seq = flycast.memory.read32(bloom_gte_address)
    if seq == 0 or seq % 2 ~= 0 or seq == last then return end
    local words = {}
    for i = 1, 45 do words[i] = flycast.memory.read32(bloom_gte_address + i*4) end
    if flycast.memory.read32(bloom_gte_address) ~= seq then return end
    if words[1] < 5000000 or words[1] > 60000000 then return end
    local total = 0
    for i = 2, 23 do total = total + words[i] end
    if total > words[1] then return end
    out = assert(io.open(bloom_gte_output, 'a'))
    out:write(tostring(os.time()), ',', tostring(seq))
    for _, value in ipairs(words) do out:write(',', tostring(value)) end
    out:write('\n'); out:close(); last = seq
end
