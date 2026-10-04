-- Read-only optional register-I/O attribution; use symbols from this ELF.
assert(type(bloom_io_address) == 'number', 'Missing I/O snapshot address')
assert(type(bloom_io_output) == 'string', 'Missing I/O output path')
local existing = io.open(bloom_io_output, 'r')
if existing then existing:close(); error('Output already exists') end
local out = assert(io.open(bloom_io_output, 'w'))
out:write('host_time,sequence,duration_us,kind,offset,mode,calls,usec\n')
out:close()
local previous, last = bloom_test_observer, 0
local kinds = {'read8','read16','read32','write8','write16','write32'}
bloom_test_observer = function()
    if previous then previous() end
    local address = bloom_io_address
    local seq = flycast.memory.read32(address)
    if seq == 0 or seq % 2 ~= 0 or seq == last then return end
    local duration = flycast.memory.read32(address+4)
    if duration < 5000000 or duration > 60000000 then return end
    local dropped_calls = flycast.memory.read32(address+8)
    local dropped_us = flycast.memory.read32(address+12)
    local rows, total = {}, dropped_us
    for i = 0, 127 do
        local entry = address + 16 + i*12
        local calls = flycast.memory.read32(entry+4)
        if calls ~= 0 then
            local key = flycast.memory.read32(entry)
            local kind = math.floor(key/65536)%256
            local mode = math.floor(key/16777216)
            local us = flycast.memory.read32(entry+8)
            if kind > 5 or mode > 4 then return end
            rows[#rows+1] = string.format('%s,%04x,%u,%u,%u', kinds[kind+1], key%65536, mode, calls, us)
            total = total + us
        end
    end
    if flycast.memory.read32(address) ~= seq or total > duration then return end
    rows[#rows+1] = string.format('dropped,0000,0,%u,%u', dropped_calls, dropped_us)
    out = assert(io.open(bloom_io_output, 'a'))
    for _, row in ipairs(rows) do
        out:write(tostring(os.time()), ',', tostring(seq), ',', tostring(duration), ',', row, '\n')
    end
    out:close(); last = seq
end
