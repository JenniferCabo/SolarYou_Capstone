"""Compile both full-control integration units with the actual ESP32 flags.

This does not replace the motor-off build objects and is not a separate
full-control firmware link. Run after the default firmware build.
"""
import ctypes
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
build = ROOT / 'firmware/ESP_SolarYou/build-ble'
entries = json.loads((build / 'compile_commands.json').read_text())
tmp = ROOT / 'tmp'
tmp.mkdir(exist_ok=True)
parse_command = ctypes.windll.shell32.CommandLineToArgvW
parse_command.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
parse_command.restype = ctypes.POINTER(ctypes.c_wchar_p)
environment = dict(os.environ, TEMP=str(tmp), TMP=str(tmp))
results = []
for filename in ('ESP_SolarYou.c', 'ble_app.c'):
    entry = next(e for e in entries if e['file'].replace('\\', '/').endswith('/main/' + filename))
    argument_count = ctypes.c_int()
    parsed = parse_command(entry['command'], ctypes.byref(argument_count))
    arguments = [parsed[i] for i in range(argument_count.value)]
    ctypes.windll.kernel32.LocalFree(parsed)
    compiler = arguments.pop(0)
    stem = Path(filename).stem
    arguments[arguments.index('-o') + 1] = str(tmp / (stem + '-full-control.obj')).replace('\\', '/')
    if '-MF' in arguments:
        arguments[arguments.index('-MF') + 1] = str(tmp / (stem + '-full-control.d')).replace('\\', '/')
    arguments.extend(['-DCONFIG_SOLARYOU_RUNTIME_FULL_CONTROL=1', '-Werror'])
    response = tmp / (stem + '-full-control.rsp')
    response.write_text('\n'.join('"' + a.replace('\\', '\\\\').replace('"', '\\"') + '"' for a in arguments), encoding='utf-8')
    result = subprocess.run([compiler, '@' + str(response)], cwd=entry['directory'], env=environment,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    item = {'source': filename, 'passed': result.returncode == 0,
            'compiler': compiler, 'output': result.stdout}
    results.append(item)
    print(filename + ': ' + ('PASS' if item['passed'] else 'FAIL'))
    if result.stdout:
        print(result.stdout)
report = {'passed': all(r['passed'] for r in results),
          'scope': 'Full-control main and BLE bridge translation units; not a separate firmware link',
          'sources': results}
(ROOT / 'artifacts/full-control-compile.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
raise SystemExit(0 if report['passed'] else 1)
