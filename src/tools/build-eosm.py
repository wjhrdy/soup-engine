#!/usr/bin/env python3
"""Build the personal EOSM.202 port against a pinned official ML checkout."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
ML_URL = "https://github.com/reticulatedpines/magiclantern_simplified.git"
ML_COMMIT = "142472e95054d73640f50789ac0fd66325499d82"


def run(*args, cwd=None):
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ml", type=Path, default=ROOT / "vendor/magiclantern-eosm")
    parser.add_argument("--arm-bin", type=Path,
                        default=Path(shutil.which("arm-none-eabi-gcc") or "").resolve().parent)
    parser.add_argument("--ar", default=None, help="GNU ARM ar with --output support")
    args = parser.parse_args()
    ml = args.ml.resolve()
    if not (ml / ".git").exists():
        if ml.exists(): parser.error(f"Not a Git checkout: {ml}")
        run("git", "clone", "--depth", "1", ML_URL, ml)
    actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ml, text=True).strip()
    if actual != ML_COMMIT:
        parser.error(f"Expected ML {ML_COMMIT}; checkout is {actual}. Use a separate checkout at the pin.")
    if subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ml):
        parser.error("ML has tracked changes. Build from an unmodified checkout.")
    for tool in ("gcc", "ld", "objcopy", "strip", "readelf", "ar"):
        if not (args.arm_bin / f"arm-none-eabi-{tool}").is_file():
            parser.error(f"Missing ARM tool in {args.arm_bin}: {tool}")

    module = ml / "modules/soup"
    module.mkdir(parents=True, exist_ok=True)
    for name in ("soup.c", "develop.c", "platform.h", "Makefile", "README.rst"):
        shutil.copy2(ROOT / "src/ml/eosm" / name, module / name)
    shutil.copytree(ROOT / "src/engine", module / "engine", dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("*.o", "*.d"))
    common = [f"ARM_BINPATH={args.arm_bin}",
              f"READELF={args.arm_bin / 'arm-none-eabi-readelf'}"]
    if args.ar: common.append(f"AR={Path(args.ar).resolve()}")
    ptp = ["CONFIG_PTP=n", "CONFIG_PTP_ML=n", "CONFIG_PTP_CHDK=n"]
    # Build the new adapter with warnings as errors. Existing ML core has
    # warnings with GCC 9, so its documented FATAL_WARNINGS=n option is used.
    run("make", "clean", *common, cwd=module)
    run("make", "-j4", *common, cwd=module)
    cam = ml / "platform/EOSM.202"
    stat = "stat -f %z" if sys.platform == "darwin" else "stat -c %s"
    # Build the modules selected by this camera, including its RAW video
    # modules, rather than shipping a new core beside old-ABI modules.
    module_paths = {"edmac": "dev_tools/edmac",
                    **{n: f"raw_video/{n}" for n in ("mlv_lite", "mlv_play", "mlv_snd")}}
    included = [s.strip() for s in (cam / "modules.included").read_text().splitlines()
                if s.strip() and not s.startswith("#")]
    selected = "ML_MODULES=" + " ".join(module_paths.get(n, n) for n in included)
    # Make does not track command-line CFLAGS changes between PTP variants.
    run("make", "clean", *common, cwd=cam)
    run("make", "-j4", *common, *ptp, "FATAL_WARNINGS=n", f"STAT_CMD={stat}",
        selected, "build/cam_complete", cwd=cam)

    # Resolve every module import against this exact core before packaging.
    nm = args.arm_bin / "arm-none-eabi-nm"
    imports = subprocess.check_output([str(nm), "-u", str(module / "build/soup.mo")], text=True)
    symbols = {line.split()[1] for line in (cam / "build/EOSM_202.sym").read_text().splitlines()}
    missing = {line.split()[-1] for line in imports.splitlines() if line.strip()} - symbols
    if missing: raise SystemExit(f"Module imports absent from EOSM_202.sym: {sorted(missing)}")

    dist = ROOT / "dist-eosm"
    card = dist / "card"
    # Never accept a memory card path: this script only stages local output.
    if card.exists(): shutil.rmtree(card)
    shutil.copytree(cam / "build/zip", card,
                    ignore=shutil.ignore_patterns("ML-SETUP.FIR"))
    shutil.copy2(cam / "build/autoexec.bin", card / "autoexec.bin")
    shutil.copy2(cam / "build/EOSM_202.sym", card / "ML/modules/EOSM_202.sym")
    shutil.copy2(module / "build/soup.mo", card / "ML/modules/soup.mo")
    shutil.copytree(ROOT / "src/stocks", card / "ML/FILM")
    # ML's directory-target recipe can skip these after a core-only build
    # has already made empty zip directories. Always stage the actual assets.
    assets = [("data/fonts", "ML/fonts", "*.rbf"),
              ("data/vram", "ML/data", "*.lut"),
              ("data/cropmks", "ML/cropmks", "*.bmp"),
              ("scripts", "ML/scripts", "*.lua"),
              ("scripts/lib", "ML/scripts/lib", "*.lua")]
    for source, target, pattern in assets:
        (card / target).mkdir(parents=True, exist_ok=True)
        for asset in (ml / source).glob(pattern):
            shutil.copy2(asset, card / target / asset.name)
    shutil.copy2(ROOT / "EOSM.md", card / "READ-ME-EOSM.txt")
    shutil.copy2(ROOT / "LICENSE", card / "SOUP-LICENSE.txt")
    shutil.copy2(ml / "COPYING", card / "ML-LICENSE.txt")
    manifest = {
        "camera": "Original Canon EOS M", "firmware": "2.0.2", "module_api": 8,
        "usb_debug": False,
        "hardware_tested": True,
        "hardware_validation": {"status": "limited user report", "date": "2026-10-27",
            "observed": "Module loads and produces film JPEGs after I/O fix",
            "reported_seconds_per_photo": 45, "output_size_confirmed": False},
        "magic_lantern_source": ML_URL, "magic_lantern_commit": actual,
        "soup_engine_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "port": "Personal EOS M adapter in src/ml/eosm; generated with OpenAI Codex",
        "soup_worktree_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
        "adapter_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in sorted((ROOT / "src/ml/eosm").glob("*")) if p.is_file()},
        "compiler": subprocess.check_output([str(args.arm_bin / "arm-none-eabi-gcc"), "--version"], text=True).splitlines()[0],
        "sha256": {str(p.relative_to(card)): hashlib.sha256(p.read_bytes()).hexdigest()
                   for p in sorted(card.rglob("*")) if p.is_file()},
    }
    (card / "BUILD.json").write_text(json.dumps(manifest, indent=2) + "\n")
    archive = dist / "soup-eosm-202-experimental.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for p in sorted(card.rglob("*")):
            if p.is_file(): z.write(p, p.relative_to(card))
    print(f"Built {archive}\nLimited camera testing. Read EOSM.md before copying to an EOS M card.")


if __name__ == "__main__":
    main()
