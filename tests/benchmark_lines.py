#!/usr/bin/env python3
"""Compare line interpolation on the host; this does not measure game FPS."""
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
    source = (ROOT / "tests/pvr_lines.c").read_text().replace(
        "/* Production functions are inserted here by test_regressions.py. */",
        functions("src/pvr.c", ["sw_line_advance", "sw_line"]))
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "lines.c"
        path.write_text(source)
        binary = Path(directory) / "lines"
        subprocess.run([compiler, "-std=gnu11", "-O2", "-DBLOOM_BENCHMARK",
                        str(path), "-o", str(binary)], check=True)
        timings = {"reference": [], "incremental": []}
        checksums = set()
        for run in range(5):
            order = list(timings) if run % 2 == 0 else list(reversed(timings))
            for name in order:
                output = subprocess.check_output([str(binary), name], text=True).strip()
                timings[name].append(float(output.split()[0]))
                checksums.add(output.split("checksum=")[1])
        if len(checksums) != 1:
            raise SystemExit("Benchmark output differs between implementations")
        medians = {name: statistics.median(values) for name, values in timings.items()}
        for name, elapsed in medians.items():
            print(f"{name}: {elapsed:.6f} seconds (median of 5, 128000 lines)")
        print(f"Host interpolation speedup: {medians['reference'] / medians['incremental']:.2f}x")
        print("Shader calls are recorded, not rendered; this is not game FPS.")


if __name__ == "__main__":
    main()
