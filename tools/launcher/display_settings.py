"""Display choices for the original game's documented -w/-f/-r startup options.

Read the primary display in physical pixels; never change Windows display settings
or edit SPORE's preferences/saves. Native option provenance: docs/launcher.md.
"""
from __future__ import annotations

import ctypes
from ctypes import wintypes
import re


def dimensions(value):
    if not isinstance(value, str) or not re.fullmatch(r"[1-9][0-9]{2,3}x[1-9][0-9]{2,3}", value):
        raise ValueError("Choose a valid display resolution.")
    width, height = map(int, value.split("x"))
    if not (640 <= width <= 8192 and 480 <= height <= 8192):
        raise ValueError("Choose a resolution between 640 x 480 and 8192 x 8192.")
    return width, height


def preferences(value=None):
    if value is None:
        return {"mode": "game", "resolution": "desktop"}
    if not isinstance(value, dict) or set(value) != {"mode", "resolution"}:
        raise ValueError("Saved display settings are invalid. Save new choices in Settings > Display.")
    mode, resolution = value["mode"], value["resolution"]
    if mode not in ("game", "fullscreen", "windowed"):
        raise ValueError("Choose Fullscreen, Windowed or Use game settings.")
    if resolution != "desktop":
        dimensions(resolution)
    return {"mode": mode, "resolution": "desktop" if mode == "game" else resolution}


class DevMode(ctypes.Structure):
    # DEVMODEW, including its 16-byte printer/display union. dmSize excludes driver data.
    _fields_ = [("dmDeviceName", wintypes.WCHAR * 32),
                ("dmSpecVersion", wintypes.WORD), ("dmDriverVersion", wintypes.WORD),
                ("dmSize", wintypes.WORD), ("dmDriverExtra", wintypes.WORD),
                ("dmFields", wintypes.DWORD), ("modeUnion", ctypes.c_byte * 16),
                ("dmColor", wintypes.SHORT), ("dmDuplex", wintypes.SHORT),
                ("dmYResolution", wintypes.SHORT), ("dmTTOption", wintypes.SHORT),
                ("dmCollate", wintypes.SHORT), ("dmFormName", wintypes.WCHAR * 32),
                ("dmLogPixels", wintypes.WORD), ("dmBitsPerPel", wintypes.DWORD),
                ("dmPelsWidth", wintypes.DWORD), ("dmPelsHeight", wintypes.DWORD),
                ("dmDisplayFlags", wintypes.DWORD), ("dmDisplayFrequency", wintypes.DWORD),
                ("dmICMMethod", wintypes.DWORD), ("dmICMIntent", wintypes.DWORD),
                ("dmMediaType", wintypes.DWORD), ("dmDitherType", wintypes.DWORD),
                ("dmReserved1", wintypes.DWORD), ("dmReserved2", wintypes.DWORD),
                ("dmPanningWidth", wintypes.DWORD), ("dmPanningHeight", wintypes.DWORD)]


def display_modes():
    """Read primary-monitor modes, deduplicating refresh rates. No desktop mutation."""
    enum = ctypes.WinDLL("user32", use_last_error=True).EnumDisplaySettingsW
    enum.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, ctypes.POINTER(DevMode)]
    enum.restype = wintypes.BOOL

    def read(index):
        mode = DevMode()
        mode.dmSize = ctypes.sizeof(DevMode)
        return mode if enum(None, index, ctypes.byref(mode)) else None

    current = read(0xFFFFFFFF)  # ENUM_CURRENT_SETTINGS; physical, not DPI-scaled pixels.
    if current is None:
        raise OSError("Could not read your display. Try opening Settings > Display again.")
    desktop = f"{current.dmPelsWidth}x{current.dmPelsHeight}"
    dimensions(desktop)
    available = {desktop}
    for index in range(16384):
        mode = read(index)
        if mode is None:
            break
        if mode.dmBitsPerPel != 32:
            continue
        value = f"{mode.dmPelsWidth}x{mode.dmPelsHeight}"
        try:
            dimensions(value)
        except ValueError:
            continue
        available.add(value)
    return {"desktop": desktop, "resolutions": sorted(available, key=dimensions, reverse=True)}


def resolve(value, modes=None):
    selected = preferences(value)
    if selected["mode"] == "game":
        return {**selected, "arguments": []}
    modes = display_modes() if modes is None else modes
    resolution = modes["desktop"] if selected["resolution"] == "desktop" else selected["resolution"]
    width, height = dimensions(resolution)
    if resolution not in modes["resolutions"]:
        raise ValueError(f"The saved resolution {resolution} is unavailable on your primary display. Choose Desktop in Settings > Display.")
    return {**selected, "width": width, "height": height,
            "arguments": ["--display-mode", selected["mode"], "--resolution", resolution]}
