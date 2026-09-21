#!/usr/bin/env python3
"""Host microbenchmark of flat triangles; results are not Dreamcast game FPS."""
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile

from test_regressions import ROOT, functions


def main():
    compiler = shutil.which(os.environ.get("CC", "gcc")) or shutil.which("clang")
    if not compiler:
        raise SystemExit("A host C compiler is required")
    fixture = (ROOT / "tests/pvr_triangles.c").read_text()
    production = functions("src/pvr.c", ["sw_texel", "sw_plot", "sw_shade",
                                          "sw_edge", "sw_edge_bias", "sw_triangle_flat", "sw_triangle"])
    # Route the same workload through the retained general interpolation path.
    marker = "if (!textured && v0->r == v1->r"
    if production.count(marker) != 1:
        raise SystemExit("Fast-path dispatch changed; update benchmark selection")
    sources = {
        "general": production.replace(marker, "if (false && !textured && v0->r == v1->r"),
        "flat": production,
    }
    with tempfile.TemporaryDirectory() as directory:
        binaries = {}
        for name, source in sources.items():
            path = Path(directory) / (name + ".c")
            path.write_text(fixture.replace(
                "/* Production functions are inserted here by test_regressions.py. */", source))
            binary = Path(directory) / name
            subprocess.run([compiler, "-std=gnu11", "-O2", "-DBLOOM_BENCHMARK",
                            str(path), "-o", str(binary)], check=True)
            binaries[name] = binary
        timings = {name: [] for name in sources}
        checksums = set()
        for run in range(5):
            # Alternate order to reduce warmup/thermal bias.
            for name in (list(sources) if run % 2 == 0 else list(reversed(sources))):
                output = subprocess.check_output([str(binaries[name])], text=True).strip()
                timings[name].append(float(output.split()[0]))
                checksums.add(output.split("checksum=")[1])
        if len(checksums) != 1:
            raise SystemExit("Triangle benchmark output differs between implementations")
        medians = {name: statistics.median(values) for name, values in timings.items()}
        for name, elapsed in medians.items():
            print(f"{name}: {elapsed:.6f} seconds (median of 5, 5000 triangles)")
        print(f"Host microbenchmark speedup: {medians['general'] / medians['flat']:.2f}x")
        print("This measures only solid, untextured software triangles, not game FPS.")


if __name__ == "__main__":
    main()
