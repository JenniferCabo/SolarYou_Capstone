"""Package firmware, source, dashboard, and test results.

Run after building the sensor/dashboard and immediate-start ESP-only modes.
Stage new source files or add them with intent-to-add so the patch includes them.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--dashboard", required=True, type=Path)
args = parser.parse_args()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
archive_path = output / "SolarYou_BLE_ESP_Only_Test.zip"
if archive_path.exists():
    raise SystemExit(f"Archive already exists: {archive_path}")
builds = [("firmware-motor-off", ROOT / "ESP_SolarYou/build-ble-sensordashboard", False),
          ("firmware-esp-only", ROOT / "ESP_SolarYou/build-ble-esponly", True)]
for prefix, build, esp_only in builds:
    flash = json.loads((build / "flasher_args.json").read_text())
    config = (build / "sdkconfig").read_text()
    if ("CONFIG_SOLARYOU_RUNTIME_ESP_ONLY_TEST=y" in config) != esp_only:
        raise SystemExit(f"Wrong runtime configuration for {prefix}")
    if "CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL=y" in config:
        raise SystemExit(f"Motors must be disabled in {prefix}")
    for filename in flash["flash_files"].values():
        if not (build / filename).is_file():
            raise SystemExit(f"Build output missing: {prefix}/{filename}")

source_paths = subprocess.check_output(
    ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=ROOT
).decode().split("\0")
patch = subprocess.check_output(["git", "diff", "--binary", "HEAD"], cwd=ROOT)
(output / "bluetooth-integration.patch").write_bytes(patch)
start = """# SolarYou BLE firmware

The BLE integration uses the main firmware's sensor readings and Zack's existing
dashboard receiver. Setup is in source/BLE_INTEGRATION.md; completed tests and
remaining hardware checks are in source/BLE_VALIDATION.md.

Choose the firmware for the test:

- firmware-motor-off/: real sensor readings, motors disabled.
- firmware-esp-only/: generated USB/BLE readings, no sensors or motors needed.

Both images target ESP32-S3 with 16 MB flash. Use the selected folder's flash_args
from that directory. The full-control build instructions are in the setup guide.

For an ESP-only Bluetooth test after flashing:

    python -m venv .venv
    ./.venv/Scripts/python.exe -m pip install -r dashboard/requirements.txt
    ./.venv/Scripts/python.exe source/tools/run_esp_only_dashboard.py

Open http://localhost:5001. The launcher connects to SolarYou-ESP-TEST, labels
the generated readings, and disables automatic CSV logging. Packets are marked
simulated=true. The included dashboard receiver is unchanged.

- source/: integrated firmware, tests, and build instructions.
- bluetooth-integration.patch: changes against team main cb263b8471ce167d7e51c4670b35fe2818bffeb7.
- dashboard/: Zack's dashboard at 79151e66aeb2835a95837fa0fcf31abd292a579e.
- firmware-motor-off/: ESP32-S3 firmware built with real sensors and motor outputs disabled.
- firmware-esp-only/: ESP32-S3 firmware with generated readings; no sensors or motors needed.
- evidence/: software-check results and the actual resolved build dependencies.

"""
(output / "START_HERE.md").write_text(start, encoding="utf-8")
digests = {}
with zipfile.ZipFile(archive_path, "x", compression=zipfile.ZIP_DEFLATED) as archive:
    archive.writestr("START_HERE.md", start)
    archive.writestr("bluetooth-integration.patch", patch)
    for relative in sorted(set(source_paths)):
        if relative and (ROOT / relative).is_file():
            archive.write(ROOT / relative, "source/" + relative)
    for path in args.dashboard.resolve().rglob("*"):
        relative = path.relative_to(args.dashboard.resolve())
        if (path.is_file() and not set(relative.parts) & {"__pycache__", "logs", ".git"}
                and path.name != "config.json" and path.suffix != ".pyc"):
            archive.write(path, "dashboard/" + relative.as_posix())
    for prefix, build, _esp_only in builds:
        flash = json.loads((build / "flasher_args.json").read_text())
        build_files = list(flash["flash_files"].values()) + ["flash_args", "flasher_args.json", "sdkconfig"]
        for relative in build_files:
            path = build / relative
            archive.write(path, prefix + "/" + relative)
            digests[prefix + "/" + relative] = hashlib.sha256(path.read_bytes()).hexdigest()
    for filename in ("host-checks-esp-only.log", "dashboard-tests.log", "esp-only-dashboard-tests.log",
                     "runtime-compile.json", "esp-only-symbols.json", "build-real-sensors-v2.log", "build-esp-only.log", "build-dependencies.lock"):
        archive.write(ROOT / "artifacts" / filename, "evidence/" + filename)
    manifest = {"base_commit": "cb263b8471ce167d7e51c4670b35fe2818bffeb7",
                "dashboard_commit": "79151e66aeb2835a95837fa0fcf31abd292a579e",
                "integration_patch_sha256": hashlib.sha256(patch).hexdigest(),
                "mode": "Two images: real sensors with motors disabled; ESP-only generated-data Bluetooth test",
                "firmware_sha256": digests}
    archive.writestr("manifest.json", json.dumps(manifest, indent=2) + "\n")
with zipfile.ZipFile(archive_path) as archive:
    bad = archive.testzip()
    if bad:
        raise SystemExit(f"Archive verification failed: {bad}")
print(json.dumps({"archive": str(archive_path), "bytes": archive_path.stat().st_size,
                  "sha256": hashlib.sha256(archive_path.read_bytes()).hexdigest()}, indent=2))
