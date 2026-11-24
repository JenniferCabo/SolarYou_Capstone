"""Interactive BLE stand-in using the compiled production C codec/session model.

This does not emulate a physical radio, NimBLE or a control/physics model.
Generated readings and controller targets exercise the actual dashboard
connection adapter, notifications, settings, readback and reconnects. Angles
share an illustrative startup-neutral reference, not an absolute level datum.
"""
import asyncio
import ctypes as ct
import math
from pathlib import Path
import time

from ble_transport import BleLink, RATE_UUID

ROOT = next((p for p in Path(__file__).resolve().parents if (p / 'upstream-manifest.json').exists()),
            Path(__file__).resolve().parents[1])


class Sample(ct.Structure):
    _fields_ = [(name, ct.c_float) for name in ('pitch', 'roll', 'power', 'pitch_target', 'roll_target')] + [
        ('imu_valid', ct.c_bool), ('power_valid', ct.c_bool), ('targets_valid', ct.c_bool),
        ('captured_ms', ct.c_uint32)]


class Session(ct.Structure):
    _fields_ = [('connected', ct.c_bool), ('subscribed', ct.c_bool), ('mtu', ct.c_uint16),
                ('rate_hz', ct.c_uint), ('generation', ct.c_uint32), ('next_due_ms', ct.c_uint32)]


def make_sample(t, captured_ms):
    """Generate repeatable display cases; this is not simulated motor feedback.

    Each 20-second cycle includes available nonzero targets, errors inside and
    outside the dashboard's display band, missing IMU/targets/power, and a real
    zero target. Real motor-off firmware has no controller target and sends
    null instead; this demo also illustrates full-control-style telemetry.
    """
    phase = t % 20
    zero_target = 16 <= phase < 18
    pitch_target, roll_target = (0.0, 0.0) if zero_target else (8.0, -4.0)
    pitch_error = 0.5 * math.sin(t * 1.3)
    roll_error = 0.4 * math.cos(t)
    if 8 <= phase < 12:
        pitch_error = 3.5 if phase < 10 else 1.0
        roll_error = -2.5
    return Sample(
        pitch=pitch_target + pitch_error,
        roll=roll_target + roll_error,
        power=1.2 + 0.1 * math.sin(t),
        pitch_target=pitch_target,
        roll_target=roll_target,
        imu_valid=not (12 <= phase < 13),
        power_valid=t >= 3 and not (18 <= phase < 19),
        targets_valid=phase >= 2 and not (14 <= phase < 16),
        captured_ms=captured_ms)


class CProtocol:
    def __init__(self):
        dll = ROOT / 'tmp' / 'ble_protocol.dll'
        if not dll.exists():
            raise RuntimeError('Run tools/run_checks.ps1 first to build the C BLE simulator')
        self.lib = ct.CDLL(str(dll))
        self.lib.sy_telemetry_format.argtypes = [ct.c_void_p, ct.c_size_t, ct.POINTER(Sample)]
        self.lib.sy_telemetry_format.restype = ct.c_size_t
        self.lib.sy_ble_fragment_count.argtypes = [ct.c_size_t, ct.c_size_t]
        self.lib.sy_ble_fragment_count.restype = ct.c_size_t
        self.lib.sy_ble_fragment.argtypes = [ct.c_char_p, ct.c_size_t, ct.c_uint16,
                                           ct.c_size_t, ct.c_size_t, ct.c_void_p, ct.c_size_t]
        self.lib.sy_ble_fragment.restype = ct.c_size_t
        self.lib.sy_ble_parse_rate_command.argtypes = [ct.c_char_p, ct.c_size_t, ct.POINTER(ct.c_uint)]
        self.lib.sy_ble_parse_rate_command.restype = ct.c_bool
        self.lib.sy_ble_rate_reply.argtypes = [ct.c_void_p, ct.c_size_t, ct.c_uint]
        self.lib.sy_ble_rate_reply.restype = ct.c_size_t
        for name in ('init', 'connect', 'disconnect'):
            getattr(self.lib, 'sy_ble_session_' + name).argtypes = [ct.POINTER(Session)]
        self.lib.sy_ble_session_subscribe.argtypes = [ct.POINTER(Session), ct.c_bool, ct.c_uint32]
        self.lib.sy_ble_session_set_rate.argtypes = [ct.POINTER(Session), ct.c_uint, ct.c_uint32]
        self.lib.sy_ble_session_set_rate.restype = ct.c_bool
        self.lib.sy_ble_session_due.argtypes = [ct.POINTER(Session), ct.c_uint32]
        self.lib.sy_ble_session_due.restype = ct.c_bool

    def format(self, sample):
        buffer = ct.create_string_buffer(256)
        n = self.lib.sy_telemetry_format(buffer, len(buffer), ct.byref(sample))
        if not n:
            raise RuntimeError('C telemetry formatter rejected the sample')
        return buffer.raw[:n]

    def packets(self, data, sequence, limit):
        count = self.lib.sy_ble_fragment_count(len(data), limit)
        if not count:
            raise RuntimeError('C BLE framing rejected the payload')
        for index in range(count):
            buffer = ct.create_string_buffer(512)
            n = self.lib.sy_ble_fragment(data, len(data), sequence & 65535,
                                         limit, index, buffer, len(buffer))
            if not n:
                raise RuntimeError('C fragment creation failed')
            yield buffer.raw[:n]


class SimulatedPeripheral:
    def __init__(self, protocol, mtu=23):
        self.protocol = protocol
        self.mtu = mtu
        self.epoch = time.monotonic()
        self.reconnect_after = 0.0

    async def find_device_by_name(self, name, timeout):
        await asyncio.sleep(max(0, self.reconnect_after - time.monotonic()))
        return name

    def client(self, device, disconnected_callback):
        return SimulatedClient(self, disconnected_callback)


class SimulatedClient:
    def __init__(self, board, callback):
        self.board, self.callback = board, callback
        self.protocol = board.protocol
        self.state = Session()
        self.protocol.lib.sy_ble_session_init(ct.byref(self.state))
        self.task = None
        self.notify = None
        self.sequence = 0
        self.started = time.monotonic()

    def now_ms(self):
        return int((time.monotonic() - self.board.epoch) * 1000) & 0xffffffff

    async def __aenter__(self):
        self.protocol.lib.sy_ble_session_connect(ct.byref(self.state))
        self.state.mtu = self.board.mtu
        return self

    async def __aexit__(self, *args):
        self.protocol.lib.sy_ble_session_disconnect(ct.byref(self.state))
        if self.task:
            self.task.cancel()
            try:
                await self.task
            except asyncio.CancelledError:
                pass

    async def start_notify(self, uuid, callback):
        self.notify = callback
        self.protocol.lib.sy_ble_session_subscribe(ct.byref(self.state), True, self.now_ms())
        self.task = asyncio.create_task(self.pump())

    async def read_gatt_char(self, uuid):
        if uuid != RATE_UUID:
            raise ValueError('Unknown simulated characteristic')
        buffer = ct.create_string_buffer(24)
        n = self.protocol.lib.sy_ble_rate_reply(buffer, len(buffer), self.state.rate_hz)
        return buffer.raw[:n]

    async def write_gatt_char(self, uuid, data, response=True):
        value = ct.c_uint()
        if uuid != RATE_UUID or not self.protocol.lib.sy_ble_parse_rate_command(data, len(data), ct.byref(value)):
            raise ValueError('Firmware rejected the rate command')
        if not self.protocol.lib.sy_ble_session_set_rate(ct.byref(self.state), value.value, self.now_ms()):
            raise ValueError('Firmware rejected the rate')
        self.notify(None, await self.read_gatt_char(uuid))

    async def pump(self):
        while True:
            now = self.now_ms()
            if time.monotonic() - self.started > 20:
                # Simulate losing/rebooting the board; BleLink must restore its setting.
                self.protocol.lib.sy_ble_session_disconnect(ct.byref(self.state))
                self.board.reconnect_after = time.monotonic() + 1
                self.callback(self)
                return
            if self.protocol.lib.sy_ble_session_due(ct.byref(self.state), now):
                t = now / 1000
                sample = make_sample(t, now)
                data = self.protocol.format(sample)
                for packet in self.protocol.packets(data, self.sequence, self.state.mtu - 3):
                    self.notify(None, packet)
                self.sequence += 1
            await asyncio.sleep(0.01)


def simulation_loop(config, on_reading, on_status, on_rate, set_link):
    config = dict(config)
    config.setdefault('ble_name', 'SolarYou-ASSTP')
    config.setdefault('ble_characteristic', 'f2d2680c-66bc-4e24-8eac-d8013a99320b')
    board = SimulatedPeripheral(CProtocol(), mtu=23)
    link = BleLink(config, on_reading, on_status, on_rate)
    set_link(link)
    asyncio.run(link.run(board, board.client))
