"""HOST checks for the virtual display holder: a fake driver and fake monitor lists; no display is added."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

path = Path(__file__).resolve().parents[2] / 'tools/native/virtual-display.py'
spec = importlib.util.spec_from_file_location('virtual_display', path)
display = importlib.util.module_from_spec(spec); spec.loader.exec_module(display)

PRIMARY = dict(device=r'\\.\DISPLAY1', rect=[0, 0, 2560, 1440], primary=True)
SECOND = dict(device=r'\\.\DISPLAY2', rect=[-2400, 338, 0, 1688], primary=False)
VIRTUAL = dict(device=r'\\.\DISPLAY9', rect=[2560, 0, 4480, 1080], primary=False)
# With every physical monitor switched off, Windows keeps the desktop on the virtual adapter itself.
HEADLESS = dict(device=r'\\.\DISPLAY10', rect=[0, 0, 1920, 1080], primary=True, adapter='Parsec Virtual Display Adapter')


class FakeDriver:
    def __init__(self, version=45, index=0):
        self.version_value, self.index, self.calls, self.closed = version, index, [], False

    def version(self): self.calls.append('version'); return self.version_value
    def add(self): self.calls.append('add'); return self.index
    def ping(self): self.calls.append('ping')
    def remove(self, index): self.calls.append(('remove', index))
    def close(self): self.closed = True


class Clock:
    def __init__(self): self.now = 0.0
    def __call__(self): return self.now
    def sleep(self, seconds): self.now += seconds


class ProtocolTests(unittest.TestCase):
    def test_remove_request_carries_the_index_big_endian(self):
        self.assertEqual(display.remove_request(1), b'\x00\x01')
        self.assertEqual(display.remove_request(0x0102), b'\x01\x02')

    def test_control_codes_match_the_documented_driver_interface(self):
        self.assertEqual((display.IOCTL_ADD, display.IOCTL_REMOVE, display.IOCTL_UPDATE, display.IOCTL_VERSION),
                         (0x0022e004, 0x0022a008, 0x0022a00c, 0x0022e010))


class LifetimeTests(unittest.TestCase):
    def test_a_session_may_be_held_for_hours_but_never_without_a_limit(self):
        self.assertEqual(display.lifetime('28800'), 28800)
        for bad in ('4', '43201', '0', 'x'):
            with self.assertRaises(Exception):
                display.lifetime(bad)


class NewMonitorTests(unittest.TestCase):
    def test_the_monitor_that_appeared_is_returned(self):
        self.assertEqual(display.new_monitor([PRIMARY, SECOND], [PRIMARY, VIRTUAL, SECOND]), VIRTUAL)

    def test_no_new_monitor_or_several_is_refused(self):
        self.assertIsNone(display.new_monitor([PRIMARY], [PRIMARY]))
        self.assertIsNone(display.new_monitor([PRIMARY], [PRIMARY, SECOND, VIRTUAL]))


class HoldTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.info = Path(self.directory.name) / 'virtual-display.json'
        self.stop = Path(self.directory.name) / 'display.stop'
        self.clock = Clock(); self.driver = FakeDriver()
        self.monitors = [[PRIMARY, SECOND], [PRIMARY, SECOND, VIRTUAL]]

    def enumerate(self):
        return self.monitors.pop(0) if len(self.monitors) > 1 else self.monitors[0]

    def hold(self, **options):
        return display.hold(self.driver, self.info, self.stop, enumerate_monitors=self.enumerate,
                            clock=self.clock, sleep=self.clock.sleep, **options)

    def test_display_is_added_described_and_pinged_until_the_stop_file_appears(self):
        original_sleep = self.clock.sleep
        def sleep(seconds):
            original_sleep(seconds)
            if self.clock.now >= 1.0: self.stop.touch()
        self.clock.sleep = sleep
        self.assertEqual(self.hold(max_seconds=60), 0)
        info = json.loads(self.info.read_text(encoding='utf-8'))
        self.assertEqual((info['state'], info['monitor'], info['driver_version']), ('released', VIRTUAL, 45))
        self.assertGreaterEqual(self.driver.calls.count('ping'), 15)
        self.assertEqual(self.driver.calls[-1], ('remove', 0))
        self.assertTrue(self.driver.closed)

    def test_pings_stay_inside_the_driver_keepalive_window(self):
        self.stop.touch()
        self.hold(max_seconds=60)
        self.assertLess(display.PING_SECONDS, 0.1)

    def test_holder_releases_the_display_by_itself_at_its_lifetime_limit(self):
        self.assertEqual(self.hold(max_seconds=3), 0)
        self.assertGreaterEqual(self.clock.now, 3.0); self.assertLess(self.clock.now, 3.5)
        self.assertEqual(self.driver.calls[-1], ('remove', 0))

    def test_unsupported_driver_version_adds_nothing(self):
        self.driver = FakeDriver(version=12)
        self.assertEqual(self.hold(max_seconds=3), 2)
        self.assertNotIn('add', self.driver.calls)
        self.assertEqual(json.loads(self.info.read_text(encoding='utf-8'))['state'], 'unsupported_driver')

    def test_desktop_already_living_on_the_virtual_adapter_alone_is_the_display_held(self):
        self.monitors = [[HEADLESS]]
        self.assertEqual(self.hold(max_seconds=3, appear_seconds=2), 0)
        told = json.loads(self.info.read_text(encoding='utf-8'))
        self.assertEqual((told['state'], told['monitor']['rect'], told['sole_display']), ('released', [0, 0, 1920, 1080], True))
        self.assertEqual(self.driver.calls[-1], ('remove', 0))

    def test_sole_display_on_another_adapter_is_not_taken_for_the_virtual_one(self):
        self.monitors = [[dict(PRIMARY, adapter='NVIDIA GeForce')]]
        self.assertEqual(self.hold(max_seconds=60, appear_seconds=2), 3)
        self.assertEqual(json.loads(self.info.read_text(encoding='utf-8'))['state'], 'not_attached')

    def test_virtual_adapter_beside_other_monitors_still_needs_a_new_monitor(self):
        self.monitors = [[PRIMARY, HEADLESS]]
        self.assertEqual(self.hold(max_seconds=60, appear_seconds=2), 3)

    def test_display_that_never_appears_is_removed_and_reported(self):
        self.monitors = [[PRIMARY, SECOND]]
        self.assertEqual(self.hold(max_seconds=60, appear_seconds=2), 3)
        self.assertEqual(self.driver.calls[-1], ('remove', 0))
        self.assertEqual(json.loads(self.info.read_text(encoding='utf-8'))['state'], 'not_attached')

    def test_driver_refusing_to_add_is_reported(self):
        self.driver = FakeDriver(index=-1)
        self.assertEqual(self.hold(max_seconds=3), 3)
        self.assertNotIn(('remove', -1), self.driver.calls)


if __name__ == '__main__':
    unittest.main()
