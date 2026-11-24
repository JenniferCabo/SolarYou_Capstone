import asyncio
import builtins
import importlib.util
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import unittest
from unittest.mock import patch
from bleak.exc import BleakCharacteristicNotFoundError

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import app as dashboard
from ble_transport import BleLink, NotificationDecoder, RATE_UUID
from ble_simulation import CProtocol, make_sample

ROOT = next((p for p in Path(__file__).resolve().parents if (p / 'upstream-manifest.json').exists()),
            Path(__file__).resolve().parents[2])
CONFIG = {'ble_name': 'SolarYou-ASSTP',
          'ble_characteristic': 'f2d2680c-66bc-4e24-8eac-d8013a99320b'}


def fragments(data, sequence=1, limit=20):
    chunk = limit - 6
    count = (len(data) + chunk - 1) // chunk
    return [bytes([0x1e, 1, sequence & 255, sequence >> 8, i, count]) + data[i*chunk:(i+1)*chunk]
            for i in range(count)]


class DecoderTests(unittest.TestCase):
    def test_transport_remains_importable_without_bleak_for_other_sources(self):
        original_import = builtins.__import__
        def without_bleak(name, *args, **kwargs):
            if name == 'bleak.exc':
                raise ImportError('Bleak is not installed')
            return original_import(name, *args, **kwargs)
        path = Path(__file__).resolve().parents[1] / 'ble_transport.py'
        spec = importlib.util.spec_from_file_location('transport_without_bleak', path)
        module = importlib.util.module_from_spec(spec)
        with patch('builtins.__import__', side_effect=without_bleak):
            spec.loader.exec_module(module)
        self.assertTrue(module.valid_rate(5))
        self.assertEqual(module.NotificationDecoder().feed(b'{"pitch":1}')['pitch'], 1)

    def test_legacy_whole_json_and_new_fragments(self):
        decoder = NotificationDecoder()
        sample = {'pitch': 1.25, 'roll': -2, 'power': None, 'pitch_target': None, 'roll_target': None}
        data = json.dumps(sample).encode()
        self.assertEqual(decoder.feed(data), sample)
        packets = fragments(data)
        for packet in packets[:-1]:
            self.assertIsNone(decoder.feed(packet))
        self.assertEqual(decoder.feed(packets[-1]), sample)

    def test_missing_fragment_drops_sample_and_new_sample_recovers(self):
        decoder = NotificationDecoder()
        packets = fragments(b'{"pitch":1,"roll":2,"power":null}')
        decoder.feed(packets[0])
        with self.assertRaises(ValueError):
            decoder.feed(packets[2])
        result = None
        for packet in fragments(b'{"pitch":9,"roll":8,"power":null}', sequence=2):
            result = decoder.feed(packet)
        self.assertEqual(result['pitch'], 9)

    def test_timeout_reset_and_size_bound(self):
        decoder = NotificationDecoder(max_bytes=30)
        packets = fragments(b'{"pitch":1,"roll":2,"power":null}')
        decoder.feed(packets[0], now=0)
        with self.assertRaises(ValueError):
            decoder.feed(packets[1], now=3)
        with self.assertRaises(ValueError):
            decoder.feed(b'x' * 31)
        self.assertLessEqual(len(decoder.buffer), 30)

    def test_invalid_headers_and_duplicate_fragments(self):
        for bad in (b'\x1e', b'\x1e\x02\x01\x00\x00\x02x', b'\x1e\x01\x01\x00\x00\xffx'):
            with self.assertRaises(ValueError):
                NotificationDecoder().feed(bad)
        decoder = NotificationDecoder()
        packets = fragments(b'{"pitch":1,"roll":2,"power":null}')
        decoder.feed(packets[0])
        decoder.feed(packets[1])
        with self.assertRaises(ValueError):
            decoder.feed(packets[1])

    def test_random_packets_cannot_grow_unbounded(self):
        rng = random.Random(871)
        decoder = NotificationDecoder(max_bytes=256)
        for _ in range(1000):
            packet = rng.randbytes(rng.randint(0, 300))
            try:
                decoder.feed(packet)
            except (ValueError, UnicodeError, RecursionError):
                pass
            self.assertLessEqual(len(decoder.buffer), 256)

    def test_ack_is_not_a_sensor_reading(self):
        readings, rates, statuses = [], [], []
        link = BleLink(CONFIG, readings.append, lambda *s: statuses.append(s), rates.append)
        link.handle_notification(None, b'{"rate_hz":5}')
        self.assertEqual(readings, [])
        self.assertEqual(rates, [], 'An unsolicited notification is not readback confirmation')
        link.handle_notification(None, b'{"rate_hz":3}')
        self.assertEqual(len(readings), 0)
        self.assertEqual(statuses[-1][0], 'warning')


class FirmwareToDashboardTests(unittest.TestCase):
    def test_actual_c_trace_reaches_socketio_with_complete_samples(self):
        executable = ROOT / 'tmp' / 'ble_host_sim.exe'
        if not executable.exists():
            self.skipTest('Run tools/run_checks.ps1 to compile the cross-language C simulator')
        trace = subprocess.check_output([str(executable), '--trace'], text=True)
        events = [json.loads(line) for line in trace.splitlines()]
        browser = dashboard.socketio.test_client(dashboard.app)
        browser.get_received()
        rates, problems, received, received_at = [], [], [], []
        notification_at = None
        def reading(message):
            received.append(message)
            received_at.append(notification_at)
            dashboard.process_reading(message)
        link = BleLink(CONFIG, reading, lambda *s: problems.append(s), rates.append)
        for event in events:
            if event['type'] in ('disconnect', 'connect'):
                link.decoder.reset()
            else:
                notification_at = event['time_ms']
                packet = bytes.fromhex(event['hex'])
                self.assertLessEqual(len(packet), event['limit'])
                self.assertFalse(2000 <= event['time_ms'] < 2700)
                link.handle_notification(None, packet)
        browser_events = browser.get_received()
        browser.disconnect()
        sensor_events = [e['args'][0] for e in browser_events if e['name'] == 'sensor_update']
        self.assertEqual(problems, [])
        self.assertEqual(len(received), len(sensor_events))
        self.assertGreater(len(received), 20)
        self.assertEqual(rates, [], 'Notification-only traces cannot confirm an applied setting')
        self.assertTrue(any(e['limit'] == 20 for e in events if e['type'] == 'notification'))
        self.assertTrue(any(e['limit'] == 182 for e in events if e['type'] == 'notification'))
        self.assertTrue(any(r['power'] is None for r in received))
        self.assertTrue(any(r['power'] == 1.25 for r in received))
        self.assertTrue(any(r['pitch'] is None for r in received))
        self.assertTrue(all(set(r) == {'pitch', 'roll', 'power', 'pitch_target', 'roll_target'}
                            for r in sensor_events))
        self.assertTrue(any(r['pitch_target'] is None and r['roll_target'] is None for r in sensor_events))
        self.assertTrue(any(r['pitch_target'] == 2 and r['roll_target'] == -1 for r in sensor_events))
        self.assertTrue(any(r['pitch_target'] == 0 and r['roll_target'] == 0 for r in sensor_events))
        self.assertTrue(all(set(r) == {'pitch', 'roll', 'power', 'pitch_target', 'roll_target'}
                            for r in received))
        self.assertTrue(all('attitude_ref' not in r and 'sequence' not in r and 'uptime_ms' not in r
                            for r in sensor_events))
        early = [t for t in received_at if t < 1000]
        self.assertEqual(early, [0, 200, 400, 600, 800])
        slow = [t for t in received_at if 1000 <= t < 2000]
        self.assertEqual(slow, [1000, 1500])
        fast = [t for t in received_at if 3500 <= t <= 3900]
        self.assertEqual(fast, [3500, 3600, 3700, 3800, 3900])
        (ROOT / 'simulation-trace.jsonl').write_text(trace, encoding='utf-8')

    def test_browser_rate_request_validation(self):
        client = dashboard.socketio.test_client(dashboard.app)
        client.get_received()
        for value in (0, 3, True, '5', 1.5, None):
            client.emit('set_telemetry_rate', {'rate_hz': value})
            replies = [e['args'][0] for e in client.get_received() if e['name'] == 'telemetry_rate']
            self.assertEqual(replies[-1]['state'], 'error')
        client.disconnect()

    def test_legacy_json_arrays_reach_unchanged_dashboard_processor(self):
        samples = [{'pitch': 1, 'roll': 2}, {'pitch': 3, 'roll': 4}]
        data = json.dumps(samples).encode()
        for packets in ([data], fragments(data)):
            with self.subTest(fragmented=len(packets) > 1):
                browser = dashboard.socketio.test_client(dashboard.app)
                browser.get_received()
                try:
                    link = BleLink(CONFIG, dashboard.process_reading, lambda *s: None, lambda _: None)
                    for packet in packets:
                        link.handle_notification(None, packet)
                    readings = [e['args'][0] for e in browser.get_received()
                                if e['name'] == 'sensor_update']
                    self.assertEqual([reading['pitch'] for reading in readings], [1, 3])
                finally:
                    browser.disconnect()

    def test_legacy_json_array_keeps_original_batch_limit(self):
        samples = [{'pitch': 1}] + [{}] * (dashboard.MAX_BATCH_SIZE - 1) + [{'pitch': 99}]
        browser = dashboard.socketio.test_client(dashboard.app)
        browser.get_received()
        try:
            link = BleLink(CONFIG, dashboard.process_reading, lambda *s: None, lambda _: None)
            for packet in fragments(json.dumps(samples).encode(), limit=182):
                link.handle_notification(None, packet)
            readings = [e['args'][0] for e in browser.get_received() if e['name'] == 'sensor_update']
            self.assertEqual([reading['pitch'] for reading in readings], [1])
        finally:
            browser.disconnect()


class SimulationTests(unittest.TestCase):
    def test_generated_measurements_are_distinct_from_controller_targets(self):
        sample = make_sample(4.0, 4000)
        self.assertTrue(sample.imu_valid and sample.targets_valid and sample.power_valid)
        self.assertEqual((sample.pitch_target, sample.roll_target), (8, -4))
        self.assertNotEqual(sample.pitch, sample.pitch_target)
        self.assertNotEqual(sample.roll, sample.roll_target)
        self.assertLess(abs(sample.pitch - sample.pitch_target), 2)
        self.assertLess(abs(sample.roll - sample.roll_target), 2)
        self.assertEqual(sample.captured_ms, 4000)

    def test_generated_tracking_cycle_has_outside_band_and_single_axis_error(self):
        outside = make_sample(9.0, 9000)
        self.assertGreater(abs(outside.pitch - outside.pitch_target), 2)
        self.assertGreater(abs(outside.roll - outside.roll_target), 2)
        single_axis = make_sample(11.0, 11000)
        self.assertLess(abs(single_axis.pitch - single_axis.pitch_target), 2)
        self.assertGreater(abs(single_axis.roll - single_axis.roll_target), 2)

    def test_missing_values_are_explicitly_invalid_and_real_zero_target_is_valid(self):
        startup = make_sample(1.0, 1000)
        self.assertFalse(startup.targets_valid)
        self.assertFalse(startup.power_valid)
        self.assertTrue(startup.imu_valid)
        self.assertFalse(make_sample(12.5, 12500).imu_valid)
        self.assertFalse(make_sample(15.0, 15000).targets_valid)
        self.assertFalse(make_sample(18.5, 18500).power_valid)
        zero = make_sample(17.0, 17000)
        self.assertTrue(zero.targets_valid)
        self.assertEqual((zero.pitch_target, zero.roll_target), (0, 0))

    def test_generated_samples_use_production_c_null_and_zero_serialization(self):
        if not (ROOT / 'tmp/ble_protocol.dll').exists():
            self.skipTest('Run tools/run_checks.ps1 to build the production C codec DLL')
        protocol = CProtocol()
        startup = json.loads(protocol.format(make_sample(1.0, 1000)))
        self.assertEqual(set(startup), {'pitch', 'roll', 'power', 'pitch_target', 'roll_target'})
        self.assertIsNone(startup['power'])
        self.assertIsNone(startup['pitch_target'])
        self.assertIsNone(startup['roll_target'])
        self.assertIsNotNone(startup['pitch'])
        invalid_imu = json.loads(protocol.format(make_sample(12.5, 12500)))
        self.assertIsNone(invalid_imu['pitch'])
        self.assertIsNone(invalid_imu['roll'])
        self.assertEqual(invalid_imu['pitch_target'], 8)
        missing_targets = json.loads(protocol.format(make_sample(15.0, 15000)))
        self.assertIsNone(missing_targets['pitch_target'])
        self.assertIsNone(missing_targets['roll_target'])
        self.assertIsNotNone(missing_targets['pitch'])
        zero = json.loads(protocol.format(make_sample(17.0, 17000)))
        self.assertEqual((zero['pitch_target'], zero['roll_target']), (0, 0))


class FakeScanner:
    @staticmethod
    async def find_device_by_name(name, timeout):
        return name


class FakeClient:
    def __init__(self, owner, disconnected_callback):
        self.owner = owner
        self.callback = disconnected_callback
        self.rate = 5  # Each instance simulates a rebooted board.
        self.notify = None
        self.writes = []
        self.is_connected = True
        self.read_calls = 0
        self.rate_replies = iter(owner.rate_replies)
    async def __aenter__(self):
        self.owner.instances.append(self)
        return self
    async def __aexit__(self, *args):
        return False
    async def start_notify(self, uuid, callback):
        self.notify = callback
    async def read_gatt_char(self, uuid):
        self.read_calls += 1
        if not self.owner.support_rate:
            raise BleakCharacteristicNotFoundError(uuid)
        reply = next(self.rate_replies, None)
        if isinstance(reply, Exception):
            raise reply
        if reply is not None:
            return reply
        return json.dumps({'rate_hz': self.rate}).encode()
    async def write_gatt_char(self, uuid, data, response):
        self.writes.append((uuid, data, response))
        self.rate = json.loads(data)['set_rate_hz']
        # Deliberately lose ack notifications: readback must still confirm.
    def disconnect(self):
        self.is_connected = False
        self.callback(self)


class FakeFactory:
    def __init__(self, support_rate=True, rate_replies=()):
        self.instances = []
        self.support_rate = support_rate
        self.rate_replies = rate_replies
    def __call__(self, device, disconnected_callback):
        return FakeClient(self, disconnected_callback)


class ConnectionTests(unittest.IsolatedAsyncioTestCase):
    async def wait_until(self, predicate):
        async def wait():
            while not predicate():
                await asyncio.sleep(0.001)
        await asyncio.wait_for(wait(), timeout=2)

    async def test_reconnect_resubscribes_and_restores_confirmed_rate(self):
        rates, statuses = [], []
        link = BleLink(CONFIG, lambda _: None, lambda *s: statuses.append(s), rates.append, retry_seconds=0.001)
        factory = FakeFactory()
        stop = asyncio.Event()
        task = asyncio.create_task(link.run(FakeScanner, factory, stop))
        try:
            await self.wait_until(lambda: link.supports_rate)
            self.assertEqual(await link.change_rate(2), 2)
            self.assertEqual(rates[-1]['state'], 'confirmed')
            first = factory.instances[0]
            self.assertEqual(first.writes[0][0], RATE_UUID)
            self.assertLessEqual(len(first.writes[0][1]), 20)
            first.disconnect()
            await self.wait_until(lambda: len(factory.instances) == 2 and factory.instances[1].rate == 2)
            self.assertIsNotNone(factory.instances[1].notify)
            self.assertTrue(any(s[0] == 'reconnecting' for s in statuses))
        finally:
            stop.set()
            await asyncio.wait_for(task, timeout=2)
        self.assertIsNone(link.client)

    async def test_failed_rate_restore_keeps_streaming_and_allows_retry(self):
        for failure in ('write', 'readback', 'mismatch', 'timeout'):
            with self.subTest(failure=failure):
                readings, rates, statuses = [], [], []
                link = BleLink(CONFIG, readings.append, lambda *s: statuses.append(s),
                               rates.append, retry_seconds=0.001)
                fail_restore = True
                class RestoreFailureFactory(FakeFactory):
                    def __call__(self, device, disconnected_callback):
                        client = super().__call__(device, disconnected_callback)
                        if len(self.instances) == 1:
                            write, read = client.write_gatt_char, client.read_gatt_char
                            async def failing_write(uuid, data, response):
                                if fail_restore and failure == 'write':
                                    client.writes.append((uuid, data, response))
                                    raise RuntimeError('write temporarily denied')
                                await write(uuid, data, response)
                            async def failing_read(uuid):
                                raw = await read(uuid)
                                if fail_restore and client.read_calls > 1:
                                    if failure == 'readback':
                                        raise RuntimeError('readback failed')
                                    if failure == 'timeout':
                                        raise TimeoutError()
                                    if failure == 'mismatch':
                                        return b'{"rate_hz":5}'
                                return raw
                            client.write_gatt_char = failing_write
                            client.read_gatt_char = failing_read
                        return client
                factory = RestoreFailureFactory()
                stop = asyncio.Event()
                task = asyncio.create_task(link.run(FakeScanner, factory, stop))
                try:
                    await self.wait_until(lambda: link.supports_rate)
                    self.assertEqual(await link.change_rate(2), 2)
                    marker = len(rates)
                    factory.instances[0].disconnect()
                    await self.wait_until(lambda: any(r['state'] == 'error' for r in rates[marker:]))
                    client = factory.instances[1]
                    client.notify(None, b'{"pitch":3,"roll":4,"power":null}')
                    client.notify(None, b'{"rate_hz":2}')  # An advisory reply cannot erase the error.
                    await asyncio.sleep(0.03)
                    self.assertEqual(len(factory.instances), 2, 'Rate failure must not reconnect')
                    self.assertIs(link.client, client)
                    self.assertTrue(client.is_connected)
                    self.assertTrue(link.supports_rate)
                    self.assertEqual(readings[-1], {'pitch': 3, 'roll': 4, 'power': None})
                    self.assertEqual(rates[-1]['state'], 'error')
                    self.assertTrue(rates[-1]['available'], 'The user must be able to retry')
                    self.assertFalse(any(r['state'] == 'confirmed' for r in rates[marker:]))
                    self.assertEqual(sum(s[0] == 'reconnecting' for s in statuses), 1)
                    self.assertEqual(link.desired_rate, 2)
                    self.assertIsNone(link.pending_rate)
                    fail_restore = False
                    self.assertEqual(await asyncio.wrap_future(link.request_rate(2)), 2)
                    self.assertEqual(rates[-1]['state'], 'confirmed')
                    self.assertEqual(rates[-1]['rate_hz'], 2)
                    self.assertEqual(len(factory.instances), 2)
                finally:
                    stop.set()
                    await asyncio.wait_for(task, timeout=2)

    async def test_real_disconnect_during_rate_restore_still_reconnects(self):
        readings, rates, statuses = [], [], []
        link = BleLink(CONFIG, readings.append, lambda *s: statuses.append(s),
                       rates.append, retry_seconds=0.001)
        class DisconnectingRestoreFactory(FakeFactory):
            def __call__(self, device, disconnected_callback):
                client = super().__call__(device, disconnected_callback)
                if len(self.instances) == 1:
                    async def disconnecting_write(uuid, data, response):
                        client.disconnect()
                        raise RuntimeError('connection lost during restore')
                    client.write_gatt_char = disconnecting_write
                return client
        factory = DisconnectingRestoreFactory()
        stop = asyncio.Event()
        task = asyncio.create_task(link.run(FakeScanner, factory, stop))
        try:
            await self.wait_until(lambda: link.supports_rate)
            self.assertEqual(await link.change_rate(2), 2)
            marker = len(rates)
            factory.instances[0].disconnect()
            await self.wait_until(lambda: len(factory.instances) == 3
                                  and rates[-1]['state'] == 'confirmed')
            client = factory.instances[2]
            client.notify(None, b'{"pitch":7,"roll":8}')
            self.assertIs(link.client, client)
            self.assertEqual(client.rate, 2)
            self.assertEqual(readings[-1], {'pitch': 7, 'roll': 8})
            self.assertEqual(rates[-1]['rate_hz'], 2)
            self.assertFalse(any(r['state'] == 'error' for r in rates[marker:]))
            self.assertEqual(sum(s[0] == 'reconnecting' for s in statuses), 2)
        finally:
            stop.set()
            await asyncio.wait_for(task, timeout=2)

    async def test_cancel_during_rate_restore_still_stops_connection(self):
        rates = []
        link = BleLink(CONFIG, lambda _: None, lambda *s: None, rates.append)
        link.desired_rate = 2
        entered, release = asyncio.Event(), asyncio.Event()
        class WaitingRestoreFactory(FakeFactory):
            def __call__(self, device, disconnected_callback):
                client = super().__call__(device, disconnected_callback)
                async def waiting_write(uuid, data, response):
                    entered.set()
                    await release.wait()
                client.write_gatt_char = waiting_write
                return client
        task = asyncio.create_task(link.run(FakeScanner, WaitingRestoreFactory()))
        try:
            await asyncio.wait_for(entered.wait(), timeout=1)
        finally:
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await asyncio.wait_for(task, timeout=2)
        self.assertIsNone(link.client)
        self.assertIsNone(link.loop)
        self.assertIsNone(link.pending_rate)
        self.assertEqual(rates[-1]['state'], 'disconnected')
        self.assertFalse(any(r['state'] in ('confirmed', 'error') for r in rates))

    async def test_old_firmware_still_streams_without_rate_control(self):
        readings, rates = [], []
        link = BleLink(CONFIG, readings.append, lambda *s: None, rates.append, retry_seconds=0.001)
        factory = FakeFactory(support_rate=False)
        stop = asyncio.Event()
        task = asyncio.create_task(link.run(FakeScanner, factory, stop))
        try:
            await self.wait_until(lambda: any(r['state'] == 'unsupported' for r in rates))
            self.assertEqual(factory.instances[0].read_calls, 1)
            factory.instances[0].notify(None, b'{"pitch":1,"roll":2,"power":null}')
            self.assertEqual(readings[-1]['pitch'], 1)
            with self.assertRaises(RuntimeError):
                link.request_rate(5)
        finally:
            stop.set()
            await asyncio.wait_for(task, timeout=2)

    async def test_initial_rate_check_retries_while_readings_continue(self):
        readings, rates = [], []
        link = BleLink(CONFIG, readings.append, lambda *s: None, rates.append)
        factory = FakeFactory(rate_replies=(TimeoutError(), RuntimeError('temporarily busy')))
        stop = asyncio.Event()
        task = asyncio.create_task(link.run(FakeScanner, factory, stop))
        try:
            await self.wait_until(lambda: factory.instances and factory.instances[0].read_calls == 1)
            client = factory.instances[0]
            client.notify(None, b'{"pitch":1,"roll":2,"power":null}')
            self.assertEqual(readings[-1]['pitch'], 1)
            self.assertFalse(link.supports_rate)
            self.assertFalse(any(rate['state'] == 'confirmed' for rate in rates))
            await self.wait_until(lambda: link.supports_rate)
            self.assertEqual(client.read_calls, 3)
            self.assertEqual(len(factory.instances), 1)
            self.assertEqual(client.writes, [], 'Initial checks retry reads, never commands')
            self.assertEqual([rate['attempt'] for rate in rates if rate['state'] == 'checking'], [1, 2, 3])
            self.assertEqual(rates[-1]['rate_hz'], 5)
            self.assertEqual(rates[-1]['state'], 'confirmed')
        finally:
            stop.set()
            await asyncio.wait_for(task, timeout=2)

    async def test_failed_initial_checks_are_bounded_and_do_not_stop_readings(self):
        readings, rates = [], []
        link = BleLink(CONFIG, readings.append, lambda *s: None, rates.append)
        factory = FakeFactory(rate_replies=(TimeoutError(), TimeoutError(), TimeoutError()))
        stop = asyncio.Event()
        task = asyncio.create_task(link.run(FakeScanner, factory, stop))
        try:
            await self.wait_until(lambda: any(rate['state'] == 'error' for rate in rates))
            client = factory.instances[0]
            client.notify(None, b'{"pitch":3,"roll":4,"power":null}')
            client.notify(None, b'{"rate_hz":5}')
            await asyncio.sleep(0.3)
            self.assertEqual(client.read_calls, 3)
            self.assertEqual(client.writes, [])
            self.assertEqual(readings[-1]['pitch'], 3)
            self.assertFalse(link.supports_rate)
            self.assertEqual(len(factory.instances), 1)
            self.assertEqual(rates[-1]['state'], 'error')
            self.assertFalse(any(rate['state'] in ('confirmed', 'unsupported') for rate in rates))
        finally:
            stop.set()
            await asyncio.wait_for(task, timeout=2)

    async def test_reconnect_restarts_initial_rate_check_budget(self):
        rates = []
        link = BleLink(CONFIG, lambda _: None, lambda *s: None, rates.append, retry_seconds=0.001)
        factory = FakeFactory(rate_replies=(TimeoutError(), TimeoutError(), TimeoutError()))
        stop = asyncio.Event()
        task = asyncio.create_task(link.run(FakeScanner, factory, stop))
        try:
            await self.wait_until(lambda: any(rate['state'] == 'error' for rate in rates))
            first = factory.instances[0]
            factory.rate_replies = (TimeoutError(), TimeoutError())
            first.disconnect()
            await self.wait_until(lambda: len(factory.instances) == 2 and link.supports_rate)
            self.assertEqual(first.read_calls, 3)
            self.assertEqual(factory.instances[1].read_calls, 3)
            self.assertEqual([rate['attempt'] for rate in rates if rate['state'] == 'checking'],
                             [1, 2, 3, 1, 2, 3])
            self.assertEqual(rates[-1]['state'], 'confirmed')
        finally:
            stop.set()
            await asyncio.wait_for(task, timeout=2)

    async def test_disconnect_cancels_remaining_initial_checks_for_old_client(self):
        rates = []
        link = BleLink(CONFIG, lambda _: None, lambda *s: None, rates.append, retry_seconds=0.001)
        factory = FakeFactory(rate_replies=(TimeoutError(),))
        stop = asyncio.Event()
        task = asyncio.create_task(link.run(FakeScanner, factory, stop))
        try:
            await self.wait_until(lambda: factory.instances and factory.instances[0].read_calls == 1)
            first = factory.instances[0]
            factory.rate_replies = ()
            first.disconnect()
            await self.wait_until(lambda: len(factory.instances) == 2 and link.supports_rate)
            self.assertEqual(first.read_calls, 1)
            self.assertEqual(factory.instances[1].read_calls, 1)
            self.assertEqual(rates[-1]['state'], 'confirmed')
            self.assertFalse(any(rate['state'] in ('error', 'unsupported') for rate in rates))
        finally:
            stop.set()
            await asyncio.wait_for(task, timeout=2)

    async def test_invalid_initial_rate_replies_retry_without_false_unsupported(self):
        rates = []
        link = BleLink(CONFIG, lambda _: None, lambda *s: None, rates.append)
        factory = FakeFactory(rate_replies=(b'not JSON', b'{"rate_hz":3}', b'{"rate_hz":2}'))
        stop = asyncio.Event()
        task = asyncio.create_task(link.run(FakeScanner, factory, stop))
        try:
            await self.wait_until(lambda: link.supports_rate)
            self.assertEqual(factory.instances[0].read_calls, 3)
            self.assertEqual(rates[-1]['state'], 'confirmed')
            self.assertEqual(rates[-1]['rate_hz'], 2)
            self.assertFalse(any(rate['state'] in ('error', 'unsupported') for rate in rates))
        finally:
            stop.set()
            await asyncio.wait_for(task, timeout=2)

    async def test_disconnect_during_initial_readback_never_confirms(self):
        rates = []
        link = BleLink(CONFIG, lambda _: None, lambda *s: None, rates.append)
        stop = asyncio.Event()
        class DisconnectingFactory(FakeFactory):
            def __call__(self, device, disconnected_callback):
                client = super().__call__(device, disconnected_callback)
                async def read(uuid):
                    client.read_calls += 1
                    client.disconnect()
                    stop.set()
                    return b'{"rate_hz":5}'
                client.read_gatt_char = read
                return client
        factory = DisconnectingFactory()
        await asyncio.wait_for(link.run(FakeScanner, factory, stop), timeout=2)
        self.assertEqual(factory.instances[0].read_calls, 1)
        self.assertFalse(any(rate['state'] in ('confirmed', 'unsupported', 'error') for rate in rates))
        self.assertIsNone(link.client)

    async def test_invalid_rates_never_write_to_firmware(self):
        link = BleLink(CONFIG, lambda _: None, lambda *s: None, lambda _: None)
        for value in (0, 3, True, 5.0, '5'):
            with self.assertRaises(ValueError):
                await link.change_rate(value)

    async def test_notification_cannot_confirm_before_readback(self):
        rates = []
        link = BleLink(CONFIG, lambda _: None, lambda *s: None, rates.append)
        entered, release = asyncio.Event(), asyncio.Event()
        class DelayedReadback:
            async def write_gatt_char(self, uuid, data, response):
                link.handle_notification(None, b'{"rate_hz":2}')
            async def read_gatt_char(self, uuid):
                entered.set()
                await release.wait()
                return b'{"rate_hz":2}'
        link.client = DelayedReadback()
        link.supports_rate = True
        link.change_lock = asyncio.Lock()
        task = asyncio.create_task(link.change_rate(2))
        await asyncio.wait_for(entered.wait(), timeout=1)
        self.assertEqual([r['state'] for r in rates], ['pending', 'verifying'])
        release.set()
        self.assertEqual(await task, 2)
        self.assertEqual([r['state'] for r in rates], ['pending', 'verifying', 'confirmed'])

    async def test_failed_queued_request_cannot_overwrite_next_pending_rate(self):
        rates = []
        link = BleLink(CONFIG, lambda _: None, lambda *s: None, rates.append)
        first_entered, release_first = asyncio.Event(), asyncio.Event()
        second_queued, second_entered, release_second = asyncio.Event(), asyncio.Event(), asyncio.Event()
        class ObservedLock(asyncio.Lock):
            async def acquire(self):
                if self.locked():
                    second_queued.set()
                return await super().acquire()
        class Client:
            is_connected = True
            async def write_gatt_char(self, uuid, data, response):
                if json.loads(data)['set_rate_hz'] == 2:
                    first_entered.set()
                    await release_first.wait()
                    raise RuntimeError('first write failed')
                second_entered.set()
                await release_second.wait()
            async def read_gatt_char(self, uuid):
                return b'{"rate_hz":10}'
        link.client = Client()
        link.supports_rate = True
        link.loop = asyncio.get_running_loop()
        link.change_lock = ObservedLock()
        first = link.request_rate(2)
        second = None
        try:
            await asyncio.wait_for(first_entered.wait(), timeout=1)
            second = link.request_rate(10)
            await asyncio.wait_for(second_queued.wait(), timeout=1)
            release_first.set()
            with self.assertRaisesRegex(RuntimeError, 'first write failed'):
                await asyncio.wrap_future(first)
            await asyncio.wait_for(second_entered.wait(), timeout=1)
            self.assertEqual([r['state'] for r in rates], ['pending', 'error', 'pending'])
            self.assertEqual(rates[0]['requested_hz'], 2)
            self.assertEqual(rates[1]['error'], 'first write failed')
            self.assertEqual(rates[-1]['requested_hz'], 10)
            self.assertEqual(link.pending_rate, 10)
            self.assertFalse(second.done(), 'The next write must still be pending')
            release_second.set()
            self.assertEqual(await asyncio.wrap_future(second), 10)
            self.assertEqual([r['state'] for r in rates],
                             ['pending', 'error', 'pending', 'verifying', 'confirmed'])
            self.assertEqual(sum(r['state'] == 'error' for r in rates), 1)
            self.assertEqual(rates[-1]['rate_hz'], 10)
        finally:
            release_first.set()
            release_second.set()
            futures = [first] if second is None else [first, second]
            await asyncio.gather(*(asyncio.wrap_future(future) for future in futures),
                                 return_exceptions=True)

    async def test_direct_rate_change_reports_failure_and_reraises(self):
        rates = []
        link = BleLink(CONFIG, lambda _: None, lambda *s: None, rates.append)
        failure = RuntimeError('write failed')
        class Client:
            is_connected = True
            async def write_gatt_char(self, *args, **kwargs):
                raise failure
            async def read_gatt_char(self, *args, **kwargs):
                raise AssertionError('A failed write must not read back')
        link.client = Client()
        link.supports_rate = True
        link.change_lock = asyncio.Lock()
        with self.assertRaises(RuntimeError) as raised:
            await link.change_rate(2)
        self.assertIs(raised.exception, failure)
        self.assertEqual([r['state'] for r in rates], ['pending', 'error'])
        self.assertEqual(rates[-1], {'available': True, 'state': 'error', 'error': 'write failed'})
        self.assertIsNone(link.pending_rate)
        self.assertIsNone(link.desired_rate)

    async def test_late_notification_cannot_confirm_failed_or_mismatched_readback(self):
        for reply in (RuntimeError('readback failed'), b'{"rate_hz":5}'):
            with self.subTest(reply=reply):
                rates = []
                link = BleLink(CONFIG, lambda _: None, lambda *s: None, rates.append)
                class Client:
                    async def write_gatt_char(self, *args, **kwargs):
                        pass
                    async def read_gatt_char(self, *args, **kwargs):
                        if isinstance(reply, Exception):
                            raise reply
                        return reply
                link.client = Client()
                link.supports_rate = True
                link.change_lock = asyncio.Lock()
                link.loop = asyncio.get_running_loop()
                future = link.request_rate(2)
                with self.assertRaises(RuntimeError):
                    await asyncio.wrap_future(future)
                await asyncio.sleep(0)
                self.assertEqual(rates[-1]['state'], 'error')
                link.handle_notification(None, b'{"rate_hz":2}')
                self.assertEqual(rates[-1]['state'], 'error')
                self.assertFalse(any(rate['state'] == 'confirmed' for rate in rates))
                self.assertIsNone(link.desired_rate)

    async def test_disconnect_during_write_or_readback_cannot_confirm(self):
        for disconnect_at in ('write', 'read'):
            with self.subTest(disconnect_at=disconnect_at):
                rates = []
                link = BleLink(CONFIG, lambda _: None, lambda *s: None, rates.append)
                class Client:
                    is_connected = True
                    async def write_gatt_char(self, *args, **kwargs):
                        if disconnect_at == 'write':
                            self.is_connected = False
                    async def read_gatt_char(self, *args, **kwargs):
                        self.is_connected = False
                        return b'{"rate_hz":2}'
                link.client = Client()
                link.supports_rate = True
                link.change_lock = asyncio.Lock()
                with self.assertRaises(RuntimeError):
                    await link.change_rate(2)
                self.assertFalse(any(rate['state'] == 'confirmed' for rate in rates))
                self.assertFalse(any(rate['state'] == 'error' for rate in rates),
                                 'A disconnected client must not publish a request error')
                self.assertIsNone(link.desired_rate)

    async def test_old_callbacks_do_not_affect_reconnected_client(self):
        readings, rates = [], []
        link = BleLink(CONFIG, readings.append, lambda *s: None, rates.append, retry_seconds=0.001)
        factory = FakeFactory()
        stop = asyncio.Event()
        task = asyncio.create_task(link.run(FakeScanner, factory, stop))
        try:
            await self.wait_until(lambda: link.supports_rate)
            first = factory.instances[0]
            first.disconnect()
            await self.wait_until(lambda: len(factory.instances) == 2 and link.supports_rate)
            second = factory.instances[1]
            first.notify(None, b'{"pitch":99,"roll":99}')
            first.disconnect()  # A delayed callback belongs to the first connection.
            second.notify(None, b'{"pitch":1,"roll":2}')
            await asyncio.sleep(0.03)
            self.assertIs(link.client, second)
            self.assertEqual(len(factory.instances), 2)
            self.assertEqual(readings, [{'pitch': 1, 'roll': 2}])
            self.assertEqual(rates[-1]['state'], 'confirmed')
        finally:
            stop.set()
            await asyncio.wait_for(task, timeout=2)

    async def test_queued_old_request_cannot_write_to_new_client(self):
        rates = []
        link = BleLink(CONFIG, lambda _: None, lambda *s: None, rates.append)
        class Client:
            is_connected = True
            def __init__(self):
                self.writes = []
            async def write_gatt_char(self, *args, **kwargs):
                self.writes.append(args)
            async def read_gatt_char(self, *args, **kwargs):
                return b'{"rate_hz":2}'
        first, second = Client(), Client()
        link.client = first
        link.supports_rate = True
        link.loop = asyncio.get_running_loop()
        link.change_lock = asyncio.Lock()
        await link.change_lock.acquire()
        future = link.request_rate(2)
        await asyncio.sleep(0)
        link.client = second
        first.is_connected = False
        link.change_lock.release()
        with self.assertRaises(RuntimeError):
            await asyncio.wrap_future(future)
        await asyncio.sleep(0)
        self.assertEqual(second.writes, [])
        self.assertEqual(rates, [], 'Old requests may not replace the new connection status')


if __name__ == '__main__':
    unittest.main()
