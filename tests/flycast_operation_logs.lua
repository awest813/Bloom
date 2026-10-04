-- Run from the repository root: lua tests/flycast_operation_logs.lua
for _, kind in ipairs({'gte', 'dispatch'}) do
    local output = os.tmpname()
    os.remove(output)
    _G['bloom_' .. kind .. '_address'] = 0x1000
    _G['bloom_' .. kind .. '_output'] = output
    local memory = {[0] = 2, 5000000}
    for i = 2, 1283 do memory[i] = 0 end
    if kind == 'gte' then
        memory[2], memory[24] = 4000000, 123
    else
        memory[2], memory[3] = 7, 100000
        memory[4], memory[5], memory[6] = 0x80012340, 123, 4000000
        memory[7], memory[8] = 1024, 4096
    end
    local seq_reads, torn, chained = 0, false, 0
    bloom_test_observer = function() chained = chained + 1 end
    flycast = {memory = {read32 = function(address)
        local index = (address - 0x1000) / 4
        if index == 0 then
            seq_reads = seq_reads + 1
            if torn and seq_reads == 2 then return 6 end
        end
        return assert(memory[index], 'Unexpected snapshot read')
    end}}
    local function contents()
        local f = assert(io.open(output, 'r'))
        local value = f:read('*a'); f:close(); return value
    end
    local function observe() seq_reads = 0; bloom_test_observer() end
    local script = 'scripts/flycast-' .. kind .. '-log.lua'
    dofile(script)
    observe()
    assert(chained == 1, 'Previous observer was lost')
    local first = contents()
    if kind == 'dispatch' then
        assert(first:find(',80012340,123,4000000,1024,4096\n', 1, true))
        assert(first:find(',dropped,7,100000,0,0\n', 1, true))
    else
        assert(first:find(',2,5000000,4000000,', 1, true))
        assert(first:find('ncct_calls\n', 1, true))
    end
    observe(); assert(contents() == first, 'Duplicate interval')
    memory[0] = 3
    observe(); assert(contents() == first, 'In-progress interval')
    memory[0], torn = 4, true
    observe(); assert(contents() == first, 'Torn interval')
    torn = false
    memory[1] = 4999999
    observe(); assert(contents() == first, 'Short interval')
    memory[1] = 60000001
    observe(); assert(contents() == first, 'Long interval')
    memory[1] = 5000000
    local cost = kind == 'gte' and 2 or 6
    memory[cost] = 5000001
    observe(); assert(contents() == first, 'Impossible measured cost')
    memory[cost] = 4000000
    observe(); assert(#contents() > #first, 'Valid retry was lost')
    local final = contents()
    assert(not pcall(dofile, script))
    assert(contents() == final, 'Existing evidence overwritten')
    os.remove(output)
end
print('Flycast operation log checks passed')
