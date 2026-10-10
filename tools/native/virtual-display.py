"""Hold one temporary virtual display for unattended native trials.

Uses the already-installed Parsec Virtual Display Adapter through its documented
control interface (see docs/sources.md). The display exists only while this
process keeps pinging the driver: it is removed on the stop file, at the lifetime
limit, or about a second after this process dies. Installs nothing, changes no
display mode or layout, and never moves a window.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
from pathlib import Path
import struct
import time
import uuid

ADAPTER_INTERFACE = '00b41627-04c4-429e-a26e-0265cf50c8fa'
IOCTL_ADD, IOCTL_REMOVE, IOCTL_UPDATE, IOCTL_VERSION = 0x0022e004, 0x0022a008, 0x0022a00c, 0x0022e010
# Only the driver version exercised on the development machine.
SUPPORTED_VERSIONS = (45,)
# The driver unplugs its displays when it is not pinged for roughly 100 ms.
PING_SECONDS = 0.05


def lifetime(text):
    """Seconds a holder may live: a trial's few minutes up to a twelve-hour work session."""
    seconds = int(text)
    if not 5 <= seconds <= 43200:
        raise argparse.ArgumentTypeError('lifetime must be 5-43200 seconds')
    return seconds


def remove_request(index):
    return struct.pack('>H', index)


VIRTUAL_ADAPTER = 'Parsec Virtual Display Adapter'


def new_monitor(before, after):
    """The single monitor present in `after` but not in `before`, else None."""
    known = {m['device'] for m in before}
    added = [m for m in after if m['device'] not in known]
    return added[0] if len(added) == 1 else None


def sole_virtual(monitors):
    """The only monitor, when it is on the virtual adapter: the desktop already lives there.

    With every physical monitor switched off Windows keeps the desktop on the virtual adapter's own
    display, so adding the virtual monitor brings that display to life instead of adding one.
    """
    return monitors[0] if len(monitors) == 1 and VIRTUAL_ADAPTER in monitors[0].get('adapter', '') else None


def write_info(path, **values):
    temporary = Path(path).with_name(Path(path).name + '.tmp')
    temporary.write_text(json.dumps(values, indent=1) + '\n', encoding='utf-8')
    os.replace(temporary, path)


def hold(driver, info, stop, *, enumerate_monitors, clock=time.monotonic, sleep=time.sleep,
         max_seconds=360, appear_seconds=5):
    """Add a display, describe it in `info`, keep it alive until `stop` exists or max_seconds pass."""
    info, stop = Path(info), Path(stop)
    try:
        version = driver.version()
        if version not in SUPPORTED_VERSIONS:
            write_info(info, state='unsupported_driver', driver_version=version)
            return 2
        before = enumerate_monitors()
        index = driver.add()
        if index < 0:
            write_info(info, state='add_refused', driver_version=version)
            return 3
        started = clock()
        monitor = None
        try:
            while monitor is None and clock() - started < appear_seconds:
                driver.ping()
                sleep(PING_SECONDS)
                monitor = new_monitor(before, enumerate_monitors())
            sole = monitor is None and sole_virtual(enumerate_monitors()) is not None
            if sole:
                monitor = sole_virtual(enumerate_monitors())
            if monitor is None:
                write_info(info, state='not_attached', driver_version=version, index=index)
                return 3
            described = dict(driver_version=version, index=index, monitor=monitor, sole_display=sole)
            write_info(info, state='held', pid=os.getpid(), **described)
            while not stop.exists() and clock() - started < max_seconds:
                driver.ping()
                sleep(PING_SECONDS)
            write_info(info, state='released', held_seconds=round(clock() - started, 3), **described)
            return 0
        finally:
            driver.remove(index)
    finally:
        driver.close()


class ParsecDriver:
    """The installed adapter's control device."""

    def __init__(self):
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.CreateFileW.restype = ctypes.c_void_p
        self.kernel.CreateEventW.restype = ctypes.c_void_p
        path = self._interface_path()
        # GENERIC_READ|GENERIC_WRITE, shared, OPEN_EXISTING, NO_BUFFERING|OVERLAPPED|WRITE_THROUGH.
        self.handle = self.kernel.CreateFileW(ctypes.c_wchar_p(path), 0xC0000000, 3, None, 3, 0xE0000000, None)
        if self.handle in (None, ctypes.c_void_p(-1).value):
            raise OSError(ctypes.get_last_error(), 'Cannot open the virtual display adapter')

    @staticmethod
    def _interface_path():
        setup = ctypes.WinDLL('setupapi', use_last_error=True)
        setup.SetupDiGetClassDevsW.restype = ctypes.c_void_p
        guid = (ctypes.c_ubyte * 16)(*uuid.UUID(ADAPTER_INTERFACE).bytes_le)

        class Interface(ctypes.Structure):
            _fields_ = [('size', ctypes.c_uint32), ('guid', ctypes.c_ubyte * 16), ('flags', ctypes.c_uint32),
                        ('reserved', ctypes.c_void_p)]
        devices = ctypes.c_void_p(setup.SetupDiGetClassDevsW(guid, None, None, 0x12))  # PRESENT|DEVICEINTERFACE
        try:
            item = Interface(); item.size = ctypes.sizeof(Interface)
            if not setup.SetupDiEnumDeviceInterfaces(devices, None, guid, 0, ctypes.byref(item)):
                raise OSError('Parsec Virtual Display Adapter is not present')
            needed = ctypes.c_uint32()
            setup.SetupDiGetDeviceInterfaceDetailW(devices, ctypes.byref(item), None, 0, ctypes.byref(needed), None)
            detail = ctypes.create_string_buffer(needed.value)
            struct.pack_into('<I', detail, 0, 8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 6)
            if not setup.SetupDiGetDeviceInterfaceDetailW(devices, ctypes.byref(item), detail, needed, None, None):
                raise OSError(ctypes.get_last_error(), 'Cannot read the adapter interface path')
            return ctypes.wstring_at(ctypes.addressof(detail) + 4)
        finally:
            setup.SetupDiDestroyDeviceInfoList(devices)

    def _control(self, code, data=b''):
        class Overlapped(ctypes.Structure):
            _fields_ = [('internal', ctypes.c_void_p), ('internal_high', ctypes.c_void_p), ('offset', ctypes.c_uint32),
                        ('offset_high', ctypes.c_uint32), ('event', ctypes.c_void_p)]
        request = ctypes.create_string_buffer(data.ljust(32, b'\0'), 32)
        result, transferred = ctypes.c_uint32(), ctypes.c_uint32()
        overlapped = Overlapped(); overlapped.event = self.kernel.CreateEventW(None, False, False, None)
        try:
            self.kernel.DeviceIoControl(ctypes.c_void_p(self.handle), code, request, 32, ctypes.byref(result), 4,
                                        None, ctypes.byref(overlapped))
            if not self.kernel.GetOverlappedResultEx(ctypes.c_void_p(self.handle), ctypes.byref(overlapped),
                                                     ctypes.byref(transferred), 5000, False):
                return -1
            return result.value
        finally:
            self.kernel.CloseHandle(ctypes.c_void_p(overlapped.event))

    def version(self): return self._control(IOCTL_VERSION) & 0xFFFF
    def ping(self): self._control(IOCTL_UPDATE)

    def add(self):
        index = self._control(IOCTL_ADD)
        self.ping()
        return index

    def remove(self, index):
        self._control(IOCTL_REMOVE, remove_request(index))
        self.ping()

    def close(self):
        self.kernel.CloseHandle(ctypes.c_void_p(self.handle))


def enumerate_monitors():
    """Attached monitors in physical pixels: device name, (left, top, right, bottom), primary."""
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))  # per-monitor v2; harmless when already set

    class Info(ctypes.Structure):
        _fields_ = [('size', ctypes.c_uint32), ('monitor', ctypes.c_long * 4), ('work', ctypes.c_long * 4),
                    ('flags', ctypes.c_uint32), ('device', ctypes.c_wchar * 32)]
    class Device(ctypes.Structure):
        _fields_ = [('size', ctypes.c_uint32), ('name', ctypes.c_wchar * 32), ('string', ctypes.c_wchar * 128),
                    ('flags', ctypes.c_uint32), ('id', ctypes.c_wchar * 128), ('key', ctypes.c_wchar * 128)]
    adapters, index = {}, 0
    while True:
        device = Device(); device.size = ctypes.sizeof(Device)
        if not user.EnumDisplayDevicesW(None, index, ctypes.byref(device), 0):
            break
        adapters[device.name] = device.string
        index += 1
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)
    def visit(monitor, _dc, _rect, _data):
        info = Info(); info.size = ctypes.sizeof(Info)
        if user.GetMonitorInfoW(ctypes.c_void_p(monitor), ctypes.byref(info)):
            found.append(dict(device=info.device, rect=list(info.monitor), primary=bool(info.flags & 1),
                              adapter=adapters.get(info.device, '')))
        return 1
    user.EnumDisplayMonitors(None, None, visit, None)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('version')
    sub.add_parser('monitors')
    held = sub.add_parser('hold')
    held.add_argument('--info', type=Path, required=True)
    held.add_argument('--stop', type=Path, required=True)
    held.add_argument('--max-seconds', type=lifetime, default=360)
    args = parser.parse_args()
    if args.command == 'monitors':
        print(json.dumps(enumerate_monitors()))
        return 0
    driver = ParsecDriver()
    if args.command == 'version':
        try:
            print(json.dumps(dict(driver_version=driver.version(), supported=list(SUPPORTED_VERSIONS))))
        finally:
            driver.close()
        return 0
    return hold(driver, args.info, args.stop, enumerate_monitors=enumerate_monitors, max_seconds=args.max_seconds)


if __name__ == '__main__':
    raise SystemExit(main())
