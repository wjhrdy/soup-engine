#!/usr/bin/env python3
"""Run the real EOS M adapter with mocked ML I/O and a real EOS M CR2."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
SRC = ROOT / "src"
HERE = Path(__file__).resolve().parent


def run(*args, **kwargs):
    subprocess.run([str(a) for a in args], check=True,
                   env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"}, **kwargs)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("sample", type=Path, help="An original EOS M CR2 (read-only)")
    p.add_argument("--quick", action="store_true", help="Only stock 0 at half size")
    args = p.parse_args()
    sample = args.sample.resolve()
    before = hashlib.sha256(sample.read_bytes()).hexdigest()
    build = HERE / "build"
    build.mkdir(exist_ok=True)
    names = "se wb cr2 color debayer lut stock grade jpeg_enc jpeg_dec pipeline look".split()
    engine = [SRC / "engine" / (n + ".c") for n in names]
    binary = build / "test"
    run("cc", "-std=c99", "-O1", "-g", "-DSE_HOSTED", "-DSOUP_HOST_TEST",
        "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unused-function",
        "-Wno-unused-variable", "-Wno-missing-field-initializers",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I" + str(SRC / "engine"), "-I" + str(HERE.parent),
        HERE / "test.c", HERE.parent / "develop.c", *engine, "-lm", "-o", binary)
    run("make", "-C", SRC, "out/previewcheck")
    stocks = (SRC / "stocks/INDEX.TXT").read_text().splitlines()
    stocks = [s.strip() for s in stocks if s.strip() and not s.startswith("#")]
    cases = [(0, 2)] if args.quick else [(i, s) for i in range(len(stocks)) for s in (2, 1)]
    with tempfile.TemporaryDirectory(prefix="soup-eosm-") as tmp:
        tmp = Path(tmp)
        for i, scale in cases:
            card = tmp / f"card-{i}-{scale}"
            (card / "ML").mkdir(parents=True)
            (card / "ML/FILM").symlink_to(SRC / "stocks", target_is_directory=True)
            for name in ("100CANON", "101CANON"):
                (card / "DCIM" / name).mkdir(parents=True)
            actual, expected = tmp / "actual.jpg", tmp / "expected.jpg"
            actual.unlink(missing_ok=True)
            run(binary, card, sample, actual, i, scale)
            run(SRC / "out/previewcheck", sample, SRC / "stocks" / stocks[i], scale, expected)
            assert actual.read_bytes() == expected.read_bytes(), f"Output differs: {stocks[i]} scale {scale}"
            print(f"BYTE-IDENTICAL: {stocks[i]}, scale {scale}", flush=True)
    assert hashlib.sha256(sample.read_bytes()).hexdigest() == before, "Source changed"
    print(f"PASS: {len(cases)} render comparisons; CR2 unchanged ({before})")


if __name__ == "__main__":
    main()
