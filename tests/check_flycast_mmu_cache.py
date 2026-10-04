#!/usr/bin/env python3
"""Emit a focused cache-remap reproducer from Flycast's actual fastmmu.cpp.

Usage: python tests/check_flycast_mmu_cache.py /path/to/flycast > /tmp/check.cpp
       c++ -std=c++17 -fsanitize=address,undefined /tmp/check.cpp -o /tmp/check
       /tmp/check

Validated against Flycast 44e4c7b50. This isolates its translation cache, not
Bloom performance or the rest of Flycast's MMU implementation.
"""
from pathlib import Path
import sys

source = (Path(sys.argv[1]) / "core/hw/sh4/modules/fastmmu.cpp").read_text()
cache = source[source.index("struct TLB_LinkedEntry"):source.index("#if 0")]
print(r"""
#include <cstdint>
#include <cstring>
#include <iterator>
#include <cstdio>
using u32=uint32_t; using u16=uint16_t;
struct TLB_Entry {
    struct {u32 VPN=0, ASID=0;} Address;
    struct {u32 SZ0=1, SZ1=1, SH=0, PPN=0;} Data;
};
struct {u32 ASID=0;} CCN_PTEH;
""")
print(cache)
print(r"""
int main() {
    TLB_Entry a, b;
    a.Address.VPN=0xe0000000u>>10; b.Address.VPN=0xe0100000u>>10;
    a.Data.PPN=0x10000000u>>10; b.Data.PPN=0x10100000u>>10;
    cache_entry(a); cache_entry(b);
    for (unsigned i=0; i<40000; i++) {
        a.Data.PPN=(i&1 ? 0x11300000u : 0x10000000u)>>10;
        cache_entry(a); cache_entry(b);
    }
    a.Data.PPN=0x11400000u>>10;
    cache_entry(a); cache_entry(b);
    const TLB_Entry *found=nullptr;
    if (!find_entry(0xe00335c0u, &found)) return 2;
    const auto actual=(found->Data.PPN<<10)|0x335c0u;
    printf("after 80,004 remaps: expected=114335c0 actual=%08x entries=%u\n",
           actual, full_table_size);
    return actual==0x114335c0u ? 0 : 1;
}
""")
