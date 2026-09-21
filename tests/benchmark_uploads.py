#!/usr/bin/env python3
"""Host texture-upload dispatch benchmark; hardware transfer is stubbed out."""
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
    source = (ROOT / "tests/pvr_uploads.c").read_text().replace(
        "/* Production functions are inserted here by test_regressions.py. */",
        functions("src/pvr.c", ["pvr_perf_report", "update_texture"]))
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "uploads.c"
        path.write_text(source)
        binary = Path(directory) / "uploads"
        subprocess.run([compiler, "-std=gnu11", "-O2", "-DBLOOM_BENCHMARK",
                        str(path), "-o", str(binary)], check=True)
        for density in (1, 4, 64):
            timings = {"reference": [], "set-bits": []}
            checksums = set()
            for run in range(5):
                order = list(timings) if run % 2 == 0 else list(reversed(timings))
                for name in order:
                    output = subprocess.check_output([str(binary), name, str(density)], text=True).strip()
                    timings[name].append(float(output.split()[0]))
                    checksums.add(output.split("checksum=")[1])
            if len(checksums) != 1:
                raise SystemExit("Upload benchmark output differs")
            before, after = (statistics.median(timings[name]) for name in timings)
            print(f"{density:2d} blocks: {before:.6f}s -> {after:.6f}s; {before / after:.2f}x")
        print("Median of 5 runs, 5000000 uploads each. Dispatch only, not hardware transfer or game FPS.")


if __name__ == "__main__":
    main()
