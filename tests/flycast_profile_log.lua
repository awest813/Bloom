-- Run from the repository root: lua tests/flycast_profile_log.lua
for _, parts in ipairs({9, 10}) do
bloom_profile_parts = parts == 10 and 10 or nil
bloom_test_observer = nil
local output = os.tmpname()
os.remove(output)
bloom_profile_address = 0x1000
bloom_profile_output = output
local memory = {[0] = 2, 5000000, 300, 50}
for i = 4, 3 + 2 * parts do memory[i] = 0 end
memory[4] = 5000000
if parts == 10 then memory[4] = 4000000; memory[13] = 1000000 end
local reads, torn = 0, false
flycast = {memory = {read32 = function(address)
    reads = reads + 1
    if torn and reads == 5 + 2 * parts then return 6 end
    return memory[(address - bloom_profile_address) / 4]
end}}
local function contents()
    local file = assert(io.open(output, "r"))
    local text = file:read("*a")
    file:close()
    return text
end
local function observe() reads = 0; bloom_test_observer() end
dofile("scripts/flycast-profile-log.lua")
local header = contents()
assert(header:match((parts == 10 and "io" or "geometry") .. "_calls\n$"))
observe()
local first = contents()
assert(first:find(",2,5000000,300,50," .. memory[4] .. ",", 1, true))
observe()
assert(contents() == first, "Duplicate interval")
memory[0] = 3
observe()
assert(contents() == first, "In-progress interval")
memory[0] = 4
torn = true
observe()
assert(contents() == first, "Torn interval")
torn = false
memory[4] = 1
observe()
assert(contents() == first, "Invalid partition")
memory[4] = parts == 10 and 4000000 or 5000000
observe()
assert(#contents() > #first, "Valid interval after rejected read")
local final = contents()
assert(not pcall(dofile, "scripts/flycast-profile-log.lua"))
assert(contents() == final, "Existing evidence was overwritten")
os.remove(output)
end
print("Flycast profile log checks passed")
