"""Compile runtime variants with the generated ESP32-S3 compiler flags.

Run after configuring/building build-ble-sensordashboard. Uses a copied config
header per mode and separate object outputs. This is a translation-unit check,
not an independent full firmware link for those alternative configurations.
"""
import ctypes
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
build = ROOT / "ESP_SolarYou/build-ble-sensordashboard"
entries = json.loads((build / "compile_commands.json").read_text())
output = ROOT / "artifacts/runtime-compile"
output.mkdir(parents=True, exist_ok=True)
base_config = (build / "config/sdkconfig.h").read_text()

split_command = ctypes.windll.shell32.CommandLineToArgvW
split_command.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
split_command.restype = ctypes.POINTER(ctypes.c_wchar_p)
ctypes.windll.kernel32.LocalFree.argtypes = [ctypes.c_void_p]

results = []
for control, bluetooth in ((1, 1), (0, 0), (1, 0)):
    name = f"control-{control}-ble-{bluetooth}"
    config_dir = output / name
    config_dir.mkdir(exist_ok=True)
    config = "\n".join(line for line in base_config.splitlines()
                       if not line.startswith(("#define CONFIG_SOLARYOU_RUNTIME_",
                                               "#define CONFIG_SOLARYOU_BLE_TELEMETRY")))
    mode = "FULL_CONTROL" if control else "SENSOR_DASHBOARD_TEST"
    config += f"\n#define CONFIG_SOLARYOU_RUNTIME_{mode} 1\n"
    if bluetooth:
        config += "#define CONFIG_SOLARYOU_BLE_TELEMETRY 1\n"
    (config_dir / "sdkconfig.h").write_text(config)
    sources = ("ESP_SolarYou.c", "ble_app.c") if bluetooth else ("ESP_SolarYou.c",)
    for source in sources:
        entry = next(e for e in entries if e["file"].replace("\\", "/").endswith("/main/" + source))
        count = ctypes.c_int()
        parsed = split_command(entry["command"], ctypes.byref(count))
        args = [parsed[i] for i in range(count.value)]
        ctypes.windll.kernel32.LocalFree(parsed)
        old_config_path = str(build / "config").replace("\\", "/")
        args = [arg.replace("\\", "/").replace(old_config_path, config_dir.as_posix()) for arg in args]
        compiler = args.pop(0)
        args[args.index("-o") + 1] = (config_dir / (source + ".obj")).as_posix()
        if "-MF" in args:
            args[args.index("-MF") + 1] = (config_dir / (source + ".d")).as_posix()
        args.append("-Werror")
        response = config_dir / (source + ".rsp")
        response.write_text("\n".join('"' + a.replace('"', '\\"') + '"' for a in args))
        environment = dict(os.environ, TEMP=str(output), TMP=str(output))
        completed = subprocess.run([compiler, "@" + str(response)], cwd=entry["directory"],
                                   env=environment, text=True, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT)
        results.append({"configuration": name, "source": source,
                        "passed": completed.returncode == 0, "output": completed.stdout})
        print(f"{name} {source}: {'PASS' if completed.returncode == 0 else 'FAIL'}")
        if completed.stdout:
            print(completed.stdout)

report = {"passed": all(item["passed"] for item in results),
          "scope": "Alternative runtime translation units with the ESP32-S3 compiler; not separate firmware links",
          "results": results}
(ROOT / "artifacts/runtime-compile.json").write_text(json.dumps(report, indent=2) + "\n")
raise SystemExit(0 if report["passed"] else 1)
