"""Run one scripted original-game trial: authority plus two clients, closed on its own evidence.

Launches the prepared disposable workers through the existing guarded scripts,
drives a scenario from the live bridge traces, closes as soon as the scenario
finishes (or at its deadline) and writes one verdict with phase timings.
Sends no desktop input, never retries a mutation, never touches personal saves
and grants no milestone acceptance: visual review stays a separate recorded step.
"""
from __future__ import annotations

import argparse
import contextlib
import ctypes
from datetime import datetime, timedelta, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import uuid

REPO = Path(__file__).resolve().parents[2]
CONTROL = REPO / 'build/win32/Release/SporeMP.WorkerControl.exe'
# Input stops this long before the deadline so closure never races a late mutation.
CLOSING_MARGIN_SECONDS = 15
# Time allowed for the three supervisors to exit after an accepted shutdown.
SHUTDOWN_SECONDS = 20
# How long a worker that refuses its shutdown, because its game is still starting, is asked again.
STARTING_SECONDS = 40
# Each role's prepared disposable worker account and its bridge role.
ROLE_WORKERS = {'player-1': ('01', 'Replica'), 'player-2': ('02', 'Replica'), 'authority': ('03', 'Authority')}
GAME_WINDOW_TITLE = 'SPORE\u2122 Galactic Adventures'
# Longest wait for the three original game windows, and the gap between the two liveness samples.
WINDOW_SECONDS, LIVENESS_SECONDS, LIVENESS_POLL_SECONDS = 90, 45, 0.5
# A virtual display held for a whole work session (`display start`), reused by every trial.
SESSION_DISPLAY = REPO / 'local/virtual-display'
# How often the three game windows are looked for after launch.
WINDOW_POLL_SECONDS = 0.05
# Outer size of the 1280x720 windowed game and its left/top frame, as measured in trial151.
GAME_OUTER, GAME_FRAME = (1286, 755), (3, 32)
TOOLS = ('tools/native/native-trial.py', 'tools/native/virtual-display.py', 'tools/native/start-m06-server.ps1',
         'tools/native/start-worker.ps1', 'tools/native/capture-game.ps1', 'tools/native/durable-checkpoint.py',
         'tools/native/worker-personal.py', 'tools/diagnostics/sporemp_diag.py')
BINARIES = ('build/win32/Release/SporeMP.Bridge.dll', 'build/win32/Release/SporeMP.NativeHost.exe',
            'build/win32/Release/SporeMP.Coordinator.exe', 'build/win32/Release/SporeMP.WorkerControl.exe',
            'build/injector/Release/ModAPI.DLLInjector.dll', 'build/sdk/Release/SporeModAPI.dll')
# How a scenario's commands reach a creature: through the player's own key path, or straight to the network.
INPUT_CLASSES = {'input': 'INPUT_IN_PROCESS', 'click': 'INPUT_IN_PROCESS', 'point': 'INPUT_IN_PROCESS',
                 'network_action': 'DIAGNOSTIC_NETWORK_INTENT'}
# Clients first, authority last: the order every retained M11 trial used.
LAUNCH_ORDER = ('player-1', 'player-2', 'authority')
# The restored authority enforces a five-minute checkpoint budget (M09).
MIN_DEADLINE_SECONDS, MAX_DEADLINE_SECONDS = 30, 300
# High-rate per-entity samples update Trace.entities instead of being retained.
ENTITY_SAMPLES = ('scene_native_authority_sample', 'scene_native_replica_sample')
# Connection identity is derived by start-m06-server.ps1; a scenario may not restate it.
PROTECTED_CONFIG = ('host', 'port', 'build_sha256', 'executable_sha256', 'content_sha256', 'fixture_sha256')


class TrialAborted(RuntimeError):
    """The live trial can no longer be driven; closure and review still run."""


class ScenarioFailed(RuntimeError):
    """An expectation of the scenario was not met by the original game."""


class Trace:
    """Incremental reader of one live bridge JSONL trace."""

    def __init__(self, path):
        self.path = Path(path)
        self.file = self.path.open('rb')
        self.pending = b''
        self.sequence = 0
        self.network = None
        self.entities = {}
        self.bindings = {}
        self.retained = []
        self.ended = False
        self.failed = False

    def close(self):
        self.file.close()

    def poll(self):
        pieces = (self.pending + self.file.read()).split(b'\n')
        self.pending = pieces.pop()
        fresh = []
        for line in pieces:
            if not line.strip():
                continue
            row = json.loads(line)
            self.absorb(row)
            fresh.append(row)
        return fresh

    def absorb(self, row):
        if row['sequence'] != self.sequence + 1:
            raise TrialAborted(f'Non-contiguous original trace: {self.path.name}')
        self.sequence = row['sequence']
        event = row['event']
        if event in ENTITY_SAMPLES:
            self.entities[row['remote_entity']] = row
            return
        self.retained.append(row)
        if event == 'network_scene_sample':
            self.network = row
        elif event == 'bound':
            self.bindings[row['native_id']] = row
        elif event == 'network_status' and row.get('state') == 'error':
            self.failed = True
        elif event in ('trace_stop', 'network_quarantined'):
            self.ended = True


class RecordedClock:
    """Recorded time for a replay: sleeping advances it instantly."""

    def __init__(self, start_qpc, frequency):
        self.start_qpc, self.frequency, self.seconds = start_qpc, frequency, 0.0

    def __call__(self):
        return self.seconds

    def sleep(self, seconds):
        self.seconds += seconds

    def qpc(self):
        return self.start_qpc + self.seconds * self.frequency


class RecordedTrace(Trace):
    """A closed trace that hands out its rows as recorded time reaches them."""

    def __init__(self, path, clock, offset=0):
        super().__init__(path)
        self.rows = [json.loads(line) for line in self.file.read().split(b'\n') if line.strip()]
        self.clock, self.cursor, self.offset = clock, 0, offset

    def poll(self):
        fresh, limit = [], self.clock.qpc()
        while self.cursor < len(self.rows) and self.rows[self.cursor]['qpc'] + self.offset <= limit:
            row = self.rows[self.cursor]
            self.cursor += 1
            self.absorb(row)
            fresh.append(row)
        return fresh


class Live:
    """What a scenario's drive(trial) receives: blocking waits, commands and recorded expectations."""

    def __init__(self, root, roles, deadline, *, control, clock=time.monotonic, sleep=time.sleep,
                 closing_margin=CLOSING_MARGIN_SECONDS, poll_seconds=0.1, open_trace=Trace):
        self._open_trace = open_trace
        self.root = Path(root)
        self.roles = roles
        self.deadline = deadline
        self.traces = {}
        self.checks = []
        self.limits = []
        self.input_classes = set()
        self._control, self._clock, self._sleep = control, clock, sleep
        self._margin, self._poll = closing_margin, poll_seconds
        self._log = (self.root / 'sequence.jsonl').open('a', encoding='utf-8')

    def close(self):
        for trace in self.traces.values():
            trace.close()
        self._log.close()

    def record(self, event, **values):
        row = dict(event=event, utc=datetime.now(timezone.utc).isoformat(), **values)
        self._log.write(json.dumps(row, allow_nan=False) + '\n')
        self._log.flush()
        return row

    @property
    def scene(self):
        return self.traces['authority'].network['scene']

    def poll(self):
        if (self.root / 'close.claim').exists():
            raise TrialAborted('Trial is closing; no further input')
        if self.deadline - self._clock() < self._margin:
            raise TrialAborted('Too close to the deadline for further input')
        for role, meta in self.roles.items():
            if role not in self.traces:
                found = list(Path(meta['run']).glob('actors-*.jsonl'))
                if len(found) > 1:
                    raise TrialAborted(f'Ambiguous actor trace for {role}')
                if not found:
                    continue
                self.traces[role] = self._open_trace(found[0])
            trace = self.traces[role]
            trace.poll()
            if trace.failed or trace.ended:
                raise TrialAborted(f'{role} closed or reported a network error')

    def wait(self, predicate, timeout, label):
        cutoff = self._clock() + timeout
        while True:
            self.poll()
            found = predicate()
            if found:
                return found
            if self._clock() >= cutoff:
                self.record('timeout', label=label, seconds=timeout)
                raise ScenarioFailed(f'Timed out after {timeout}s: {label}')
            self._sleep(self._poll)

    def settle(self, seconds):
        """Keep the games running and watched, for example so a capture shows a final state."""
        cutoff = self._clock() + seconds
        while self._clock() < cutoff:
            self.poll()
            self._sleep(self._poll)

    def until(self, role, event, where=None, timeout=30, after=0):
        """First retained row of this event for the role, including ones already seen."""
        cursor = 0

        def scan():
            nonlocal cursor
            rows = self.traces[role].retained if role in self.traces else []
            while cursor < len(rows):
                row = rows[cursor]
                cursor += 1
                if row['event'] == event and row['sequence'] > after and (where is None or where(row)):
                    return row
            return None
        return self.wait(scan, timeout, f'{role} {event}')

    def wait_ready(self, stable_seconds=3, timeout=90):
        """All three peers ready on one scene, with no baseline change for stable_seconds."""
        held = None

        def stable():
            nonlocal held
            samples = [self.traces[role].network if role in self.traces else None for role in self.roles]
            ready = all(s and s.get('ready') and s['scene'] for s in samples) and len({s['scene'] for s in samples}) == 1
            key = tuple((s['scene'], s['baseline']) for s in samples) if ready else None
            if key is None or held is None or held[0] != key:
                held = (key, self._clock()) if key else None
                return False
            return self._clock() - held[1] >= stable_seconds
        self.wait(stable, timeout, 'all peers ready on one scene')
        self.record('ready', scene=self.scene)

    def control(self, role, op, *values, expect='accepted'):
        """One WorkerControl request. A mutation with an unknown outcome is never repeated.

        With expect=None the reply is returned whatever its result, for a command whose refusal is an answer.
        """
        meta = self.roles[role]
        command = [str(CONTROL), meta['generation'], str(meta['supervisor_pid']), op, *map(str, values)]
        if op in INPUT_CLASSES:
            self.input_classes.add(INPUT_CLASSES[op])
        self.record('control_request', role=role, command=command)
        try:
            code, stdout, stderr = self._control(command)
        except TimeoutError:
            self.record('control_outcome_unknown', role=role, command=command, retry=False)
            raise TrialAborted(f'{role} {op}: outcome unknown, not retried')
        try:
            reply = json.loads(stdout)
        except json.JSONDecodeError:
            reply = {}
        self.record('control_reply', role=role, command=command, exit_code=code, reply=reply, stderr=stderr)
        if expect is not None and (reply.get('result') != expect or (expect == 'accepted' and code)):
            raise ScenarioFailed(f'{role} {op}: {reply.get("result", "no reply")} (exit {code}), expected {expect}')
        return reply

    def key(self, role, key, down):
        """One key transition in that player's own game process (evidence label INPUT_IN_PROCESS)."""
        return self.control(role, 'input', self.scene, 1 if down else 2, ord(key) if isinstance(key, str) else key)

    def click(self, role, entity, button='left', expect='accepted', height=50):
        """Point at an entity where that player's own window draws it and click it (INPUT_IN_PROCESS).

        `height` is how high on the creature's own box to aim, in percent; a creature lying down is only hit low.
        """
        action = {'none': 1, 'left': 2, 'right': 3}[button]
        if action != 1:
            # A hand moves the pointer onto something before pressing: the game sees it hover for some frames first.
            hover = self.control(role, 'click', self.scene, 1, entity, height, expect=expect)
            if hover.get('result') != 'accepted':
                return hover
            self.settle(0.3)
        return self.control(role, 'click', self.scene, action, entity, height, expect=expect)

    def click_at(self, role, x, y, button='left', expect='accepted'):
        """Click a place in that player's own window, in client pixels: a button of the original's interface."""
        action = {'none': 1, 'left': 2, 'right': 3}[button]
        if action != 1:
            hover = self.control(role, 'point', self.scene, 1, x, y, expect=expect)
            if hover.get('result') != 'accepted':
                return hover
            self.settle(0.3)
        return self.control(role, 'point', self.scene, action, x, y, expect=expect)

    def ui(self, role, at=None):
        """Read the original's interface in that player's game: the visible windows that carry text or act as
        buttons, or with `at=(x, y)` the window under that place and its ancestors. Changes nothing."""
        mark = self.traces[role].sequence
        self.control(role, 'ui', self.scene, *((2, *at) if at else (1, 0, 0)))
        end = self.until(role, 'native_ui_snapshot', after=mark, timeout=10)
        return [r for r in self.traces[role].retained
                if r['event'] == 'native_ui_window' and mark < r['sequence'] < end['sequence']]

    def hold(self, role, key, seconds):
        """Hold a key for real time in that player's game, and always release it."""
        self.key(role, key, True)
        try:
            self.settle(seconds)
        finally:
            self.key(role, key, False)

    def expect(self, condition, label, **evidence):
        check = dict(label=label, passed=bool(condition), evidence=evidence)
        self.checks.append(check)
        self.record('expectation', **check)
        if not condition:
            raise ScenarioFailed(label)

    def not_run(self, label, prerequisite):
        self.limits.append(dict(label=label, status='NOT_RUN', prerequisite=prerequisite))


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def read_rows(path):
    return [json.loads(line) for line in Path(path).read_text(encoding='utf-8-sig').splitlines() if line.strip()]


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


class Closed:
    """What a scenario's review(closed) receives: the retained evidence of a closed trial."""

    def __init__(self, root):
        self.root = Path(root)
        self.checks = []
        self._rows = {}

    def trace_path(self, role):
        found = list((self.root / ('closed-' + role)).glob('actors-*.jsonl'))
        return found[0] if len(found) == 1 else None

    def rows(self, role):
        if role not in self._rows:
            path = self.trace_path(role)
            self._rows[role] = read_rows(path) if path else []
        return self._rows[role]

    def events(self, role, event):
        return [row for row in self.rows(role) if row['event'] == event]

    def host(self, role):
        path = self.root / ('closed-' + role) / 'native-host.jsonl'
        return read_rows(path) if path.exists() else []

    def expect(self, condition, label, **evidence):
        self.checks.append(dict(label=label, passed=bool(condition), evidence=evidence))
        return bool(condition)

    def expect_none(self, role, event):
        count = len(self.events(role, event))
        return self.expect(count == 0, f'{role}: no {event}', **({'count': count} if count else {}))

    def one(self, role, event):
        found = self.events(role, event)
        if len(found) != 1:
            self.expect(False, f'{role}: exactly one {event}', count=len(found))
            return None
        return found[0]


# Compressed bytes per pixel of a capture preview. Game frames gave 0.8 to 1.6; the all-white previews of
# trial179, taken with every monitor off, gave 0.005.
PICTURE_DETAIL = 0.05


def picture_detail(path):
    """How much picture a PNG holds, as compressed image bytes per pixel; None when it cannot be read."""
    try:
        data = Path(path).read_bytes()
        if data[:8] != b'\x89PNG\r\n\x1a\n':
            return None
        at, pixels, image = 8, 0, 0
        while at + 8 <= len(data):
            length, kind = struct.unpack('>I4s', data[at:at + 8])
            if kind == b'IHDR':
                width, height = struct.unpack('>II', data[at + 8:at + 16])
                pixels = width * height
            elif kind == b'IDAT':
                image += length
            at += 12 + length
        return image / pixels if pixels else None
    except (OSError, struct.error):
        return None


# SetThreadExecutionState: ES_CONTINUOUS with ES_SYSTEM_REQUIRED and ES_DISPLAY_REQUIRED, and ES_CONTINUOUS alone.
DISPLAY_REQUIRED, DISPLAY_RELEASED = 0x80000003, 0x80000000


def console_display_state():
    """'on', 'off' or 'dimmed' as Windows reports the console display; None when it does not answer."""
    from ctypes import wintypes
    user, kernel = ctypes.WinDLL('user32'), ctypes.WinDLL('kernel32')
    result = ctypes.c_ssize_t
    procedure_type = ctypes.WINFUNCTYPE(result, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)
    user.DefWindowProcW.restype, user.DefWindowProcW.argtypes = result, (wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)
    user.CreateWindowExW.restype = wintypes.HWND
    user.CreateWindowExW.argtypes = (wintypes.DWORD, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD, ctypes.c_int, ctypes.c_int,
                                     ctypes.c_int, ctypes.c_int, wintypes.HWND, wintypes.HANDLE, wintypes.HANDLE, wintypes.LPVOID)
    user.RegisterPowerSettingNotification.restype = wintypes.HANDLE
    user.RegisterPowerSettingNotification.argtypes = (wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD)
    kernel.GetModuleHandleW.restype = wintypes.HANDLE

    class Setting(ctypes.Structure):
        _fields_ = [('setting', ctypes.c_ubyte * 16), ('length', wintypes.DWORD), ('data', ctypes.c_ubyte * 4)]

    class WindowClass(ctypes.Structure):
        _fields_ = [('style', wintypes.UINT), ('procedure', procedure_type), ('extra', ctypes.c_int), ('window_extra', ctypes.c_int),
                    ('instance', wintypes.HANDLE), ('icon', wintypes.HANDLE), ('cursor', wintypes.HANDLE), ('brush', wintypes.HANDLE),
                    ('menu', wintypes.LPCWSTR), ('name', wintypes.LPCWSTR)]
    seen = []

    @procedure_type
    def procedure(window, message, wparam, lparam):
        if message == 0x0218 and wparam == 0x8013:  # WM_POWERBROADCAST, PBT_POWERSETTINGCHANGE
            seen.append(ctypes.cast(lparam, ctypes.POINTER(Setting)).contents.data[0])
        return user.DefWindowProcW(window, message, wparam, lparam)
    name = 'SporeMPDisplayState' + uuid.uuid4().hex
    user.RegisterClassW(ctypes.byref(WindowClass(procedure=procedure, name=name, instance=kernel.GetModuleHandleW(None))))
    window = user.CreateWindowExW(0, name, name, 0, 0, 0, 0, 0, wintypes.HWND(-3), None, None, None)  # message-only
    # GUID_CONSOLE_DISPLAY_STATE; registering delivers the current value at once.
    setting = (ctypes.c_ubyte * 16)(*uuid.UUID('6fe69556-704a-47a0-8f24-c28d936fda47').bytes_le)
    handle = user.RegisterPowerSettingNotification(window, ctypes.byref(setting), 0)
    message = wintypes.MSG()
    for _ in range(40):
        while user.PeekMessageW(ctypes.byref(message), None, 0, 0, 1):
            user.DispatchMessageW(ctypes.byref(message))
        if seen:
            break
        time.sleep(0.05)
    if handle:
        user.UnregisterPowerSettingNotification(handle)
    user.DestroyWindow(window)
    return {0: 'off', 1: 'on', 2: 'dimmed'}.get(seen[0]) if seen else None


def nudge_pointer():
    """One relative pointer input that moves by nothing: the cursor stays where it is."""
    from ctypes import wintypes

    class Mouse(ctypes.Structure):
        _fields_ = [('dx', wintypes.LONG), ('dy', wintypes.LONG), ('data', wintypes.DWORD), ('flags', wintypes.DWORD),
                    ('time', wintypes.DWORD), ('extra', ctypes.c_void_p)]

    class Input(ctypes.Structure):
        _fields_ = [('type', wintypes.DWORD), ('mouse', Mouse)]
    move = Input(type=0, mouse=Mouse(0, 0, 0, 0x0001, 0, None))
    return ctypes.WinDLL('user32').SendInput(1, ctypes.byref(move), ctypes.sizeof(Input)) == 1


@contextlib.contextmanager
def display_awake(set_state=None, *, state=console_display_state, nudge=nudge_pointer, settle=time.sleep):
    """Have Windows compose windows while a trial runs, as it does for a video player.

    Once the display has idled off, game windows are no longer composed and a window recorder receives
    a single frame (trial197). Holding the request keeps a display on but does not turn one back on
    (trial200), and neither does the monitor-power command; one pointer input that moves nothing does.
    It is sent only when Windows reports the display off, which means nobody has touched the PC for the
    whole idle timeout. Nothing is changed in the power settings; the request ends with the block.
    """
    set_state = set_state or ctypes.windll.kernel32.SetThreadExecutionState
    set_state(DISPLAY_REQUIRED)
    try:
        before = state()
        woken = before == 'off'
        if woken:
            nudge()
            settle(2)
        yield dict(display_before=before, woken=woken, display_after=state() if woken else before)
    finally:
        set_state(DISPLAY_RELEASED)


def standard_review(root, roles, *, captures=True, fixture=True):
    """Checks every closed trial must pass, whatever its scenario. Returns all results."""
    root = Path(root)
    closed = Closed(root)
    checks = []

    def check(label, condition, **evidence):
        checks.append(dict(label=label, passed=bool(condition), evidence=evidence))

    def loaded(path):
        return read_json(path) if Path(path).exists() else {}

    closure = loaded(root / 'closure-result.json')
    check('closure clean', closure.get('clean') is True and not closure.get('failures'), failures=closure.get('failures'))
    builds = set()
    for role in roles:
        rows = closed.rows(role)
        first, last = (rows[0], rows[-1]) if rows else ({}, {})
        check(f'{role}: trace starts and stops healthy',
              first.get('event') == 'trace_start' and last.get('event') == 'trace_stop'
              and last.get('healthy') is True and last.get('detach_status') == 0, rows=len(rows))
        check(f'{role}: one contiguous single-thread trace',
              bool(rows) and all(r['sequence'] == i and r['pid'] == first['pid'] and r['thread_id'] == first['thread_id']
                                 and not r['foreign_callbacks'] for i, r in enumerate(rows, 1))
              and all(a['qpc'] <= b['qpc'] for a, b in zip(rows, rows[1:])))
        check(f'{role}: no network error or trace limit',
              not closed.events(role, 'trace_limit')
              and not [r for r in closed.events(role, 'network_status') if r.get('state') == 'error'])
        exits = [r for r in closed.host(role) if r['event'] in ('game_exited', 'worker_exited')]
        check(f'{role}: game and worker exit 0', len(exits) == 2 and all(r['exit_code'] == 0 for r in exits),
              exit_codes=[r['exit_code'] for r in exits])
        personal = loaded(root / f'personal-after-{role}.json')
        check(f'{role}: personal files unchanged', personal.get('personal_sources_unchanged') is True,
              file_count=personal.get('file_count'))
        if captures:
            done = loaded(root / ('capture-' + role) / 'completion.json')
            check(f'{role}: capture finished normally', done.get('exit_code') == 0 and done.get('wall_timeout') is False)
            detail = picture_detail(root / ('capture-' + role) / 'filmstrip.png')
            check(f'{role}: capture shows a picture', detail is not None and detail >= PICTURE_DETAIL,
                  compressed_bytes_per_pixel=None if detail is None else round(detail, 4))
        builds.add((first.get('bridge_version'), first.get('sdk_commit'), first.get('executable_sha256')))
    check('all peers ran one bridge, SDK and executable', len(builds) == 1 and all(next(iter(builds))),
          builds=sorted(map(list, builds), key=str))
    if (root / 'source-hashes.json').exists():
        changed = verify_frozen(root)
        check('frozen sources and binaries match their hashes', not changed, changed=changed)
    placement = loaded(root / 'window-placement.json')
    if placement.get('placement') == 'bridge':
        windows = placement.get('windows', {})
        check('every game window opened on the parking display',
              all(windows.get(role, {}).get('opened_on_display') is True for role in roles),
              first_visible_at={role: windows.get(role, {}).get('first_visible_at') for role in roles})
        reports = {role: closed.events(role, 'native_window_placement') for role in roles}
        check('the bridge redirected the centering request in every game',
              all(len(r) == 1 and r[0].get('state') == 'attached' and r[0].get('redirected', 0) >= 1 for r in reports.values()),
              states={role: [x.get('state') for x in r] for role, r in reports.items()})
    if fixture:
        journal = loaded(root / 'fixture-restore' / 'restore.json')
        check('disposable profiles restored to their originals', journal.get('state') == 'originals_restored',
              state=journal.get('state'))
    return checks


def observations(root):
    """Recorded facts about a trial that are deliberately not pass/fail."""
    path = Path(root) / 'window-placement.json'
    windows = read_json(path).get('windows', {}) if path.exists() else {}
    held = [role for role, window in windows.items() if window.get('held_foreground')]
    if not held:
        return []
    return [dict(label='A starting game was the foreground window when it appeared', roles=held, status='ACCEPTED',
                 note='Known and left as is: happens when the user has been idle.')]


def freeze_sources(repo, root, paths):
    """Hash each repository-relative file and keep a byte-identical copy beside the trial."""
    repo, root = Path(repo), Path(root)
    entries = []
    for relative in sorted(set(str(Path(p).as_posix()) for p in paths)):
        source = repo / relative
        digest = sha256(source)
        copy = root / 'source-revisions' / relative
        copy.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, copy)
        if sha256(copy) != digest:
            raise RuntimeError(f'Frozen copy mismatch: {relative}')
        entries.append(dict(path=relative, sha256=digest))
    (root / 'source-hashes.json').write_text(json.dumps(entries, indent=1) + '\n', encoding='utf-8')
    return entries


def verify_frozen(root):
    root = Path(root)
    return [entry['path'] for entry in read_json(root / 'source-hashes.json')
            if not (root / 'source-revisions' / entry['path']).is_file()
            or sha256(root / 'source-revisions' / entry['path']) != entry['sha256']]


class Scenario:
    # A plain class: this file is loaded by path, where dataclasses cannot resolve the module.
    def __init__(self, name, path, drive, review, artifact, authority, deadline_seconds, variant=None):
        self.name, self.path, self.drive, self.review = name, path, drive, review
        self.artifact, self.authority, self.deadline_seconds = artifact, authority, deadline_seconds
        self.variant = variant
        # How verdicts and records name it: the file, and the variant when one was chosen.
        self.label = path.name + (f'[{variant}]' if variant else '')


def load_scenario(path, repo=REPO, require_artifact=True, variant=None):
    """Load and validate a scenario file before anything is launched.

    A scenario may declare VARIANTS = {name: {CONSTANT: value}}; choosing one overrides those
    module constants before anything reads them, so one file can drive either player.
    """
    path, repo = Path(path).resolve(), Path(repo).resolve()
    spec = importlib.util.spec_from_file_location('sporemp_scenario_' + path.stem.replace('-', '_'), path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if variant is not None:
        overrides = getattr(module, 'VARIANTS', {}).get(variant)
        if overrides is None or any(not hasattr(module, name) for name in overrides):
            raise ValueError(f'Scenario has no usable variant {variant!r}')
        for name, value in overrides.items():
            setattr(module, name, value)
    drive, review = getattr(module, 'drive', None), getattr(module, 'review', None)
    if not callable(drive) or (review is not None and not callable(review)):
        raise ValueError('Scenario needs drive(trial) and, optionally, review(closed)')
    deadline = getattr(module, 'DEADLINE_SECONDS', 150)
    if type(deadline) is not int or not MIN_DEADLINE_SECONDS <= deadline <= MAX_DEADLINE_SECONDS:
        raise ValueError(f'DEADLINE_SECONDS must be {MIN_DEADLINE_SECONDS}-{MAX_DEADLINE_SECONDS}')
    artifact = getattr(module, 'ARTIFACT', None)
    if artifact is not None:
        artifact = (repo / artifact).resolve()
        if not artifact.is_relative_to(repo / 'local') or (require_artifact and not artifact.is_file()):
            raise ValueError('ARTIFACT must be an existing sealed checkpoint under local/')
    authority = dict(getattr(module, 'AUTHORITY', {}))
    patch_config('', authority)
    return Scenario(path.stem, path, drive, review, artifact, authority, deadline, variant)


def compose_verdict(name, scenario, *, phases, checks, limits, error, evidence_class='NATIVE_AND_NETWORK_REAL'):
    failed = [check['label'] for check in checks if not check['passed']]
    return dict(schema_version=1, trial=name, scenario=scenario,
                outcome='PASS' if checks and not failed and error is None else 'FAIL',
                error=error, failed_checks=failed, checks=checks, limits=limits, phases=phases,
                evidence_class=evidence_class, input_class='DIAGNOSTIC_NETWORK_INTENT',
                visual_review='NOT_INSPECTED', milestone_acceptance='NONE')


class ReplayLive(Live):
    """Live for a recording: commands are noted, not sent."""

    commands_not_executed = 0
    # How the bridge records receiving an input command, and what it answered; values are (scene, action,
    # key or entity). Every pointer request leaves a screen-point row, in view or not.
    RECEIPTS = {'input': ('network_input_posted', lambda r, v: r['key'] == v[2] and r['down'] == (v[1] == 1),
                          lambda r: 'accepted' if r['posted'] else 'failed'),
                'click': ('native_screen_point', lambda r, v: r['entity'] == v[2],
                          lambda r: 'accepted' if r['inside'] else 'unavailable'),
                'point': ('network_point_posted', lambda r, v: r['action'] == v[1] and r['point'] == [v[2], v[3]],
                          lambda r: 'accepted' if r['posted'] else 'failed'),
                'ui': ('native_ui_snapshot', lambda r, v: r['mode'] == v[1], lambda r: 'accepted')}

    def __init__(self, *arguments, **options):
        super().__init__(*arguments, **options)
        self._received = {}

    def settle(self, seconds):
        try:
            super().settle(seconds)
        except TrialAborted:
            # The live run closed right after this settle, so the recording simply ends here.
            if any(trace.failed for trace in self.traces.values()):
                raise

    def control(self, role, op, *values, expect='accepted'):
        self.commands_not_executed += 1
        self.record('control_not_executed', role=role, op=op, values=list(values))
        if op in self.RECEIPTS:
            # The recording shows when the game received each key or click, and the live run sent it later
            # than a replay reaches this line. Recorded time moves to that receipt; each receipt is used once.
            event, same, answered = self.RECEIPTS[op]
            row = self.until(role, event, after=self._received.get((role, op), 0), timeout=float('inf'),
                             where=lambda r: same(r, values))
            self._received[role, op] = row['sequence']
            if expect is not None and answered(row) != expect:
                raise ScenarioFailed(f'{role} {op}: {answered(row)} in the recording, expected {expect}')
            return dict(result=answered(row), replayed=True)
        return dict(result=expect, replayed=True)


def replay_trial(scenario, root):
    """Run a scenario against a retained closed trial in recorded time. Reads only; launches nothing.

    This checks the scenario's expectations against what the original game did in
    that run. It does not exercise mod code changed since the recording was made.
    """
    root = Path(root)
    closed = Closed(root)
    first = [closed.rows(role)[0] for role in LAUNCH_ORDER if closed.rows(role)]
    errors, checks, limits, executed, timeline = [], [], [], 0, 'NONE'
    started = time.monotonic()
    if len(first) != len(LAUNCH_ORDER):
        errors.append('Retained trial lacks a closed trace for every role')
    else:
        # Each game counts time from its own trace start. From bridge 0.0.192 a trace also states where that
        # start lies on the clock all processes share, which places the three on one timeline. Older
        # recordings do not, and are replayed as if all three had started together.
        origins = [r.get('qpc_origin') for r in first]
        aligned = all(isinstance(o, int) for o in origins)
        timeline = 'ALIGNED_BY_RECORDED_ORIGIN' if aligned else 'UNALIGNED_ASSUMES_SIMULTANEOUS_START'
        offsets = {'closed-' + role: o - min(origins) if aligned else 0 for role, o in zip(LAUNCH_ORDER, origins)}
        clock = RecordedClock(min(r['qpc'] + offsets['closed-' + role] for role, r in zip(LAUNCH_ORDER, first)),
                              first[0]['qpc_frequency'])
        roles = {role: dict(run=str(root / ('closed-' + role)), generation='', supervisor_pid=0) for role in LAUNCH_ORDER}
        with tempfile.TemporaryDirectory() as scratch:
            # A finer step than the live run's, so a replay reaches each line no later than the live run did.
            live = ReplayLive(scratch, roles, float('inf'), control=None, clock=clock, sleep=clock.sleep, poll_seconds=0.01,
                              open_trace=lambda path: RecordedTrace(path, clock, offsets[Path(path).parent.name]))
            try:
                scenario.drive(live)
            except (ScenarioFailed, TrialAborted) as error:
                errors.append(str(error))
            except Exception as error:
                errors.append(f'{type(error).__name__}: {error}')
            finally:
                checks.extend(live.checks)
                limits.extend(live.limits)
                executed = live.commands_not_executed
                live.close()
    try:
        checks.extend(standard_review(root, LAUNCH_ORDER, fixture=(root / 'fixture-restore').is_dir(),
                                      captures=any((root / ('capture-' + role)).is_dir() for role in LAUNCH_ORDER)))
        if scenario.review:
            scenario.review(closed)
            checks.extend(closed.checks)
    except Exception as error:
        errors.append(f'review: {type(error).__name__}: {error}')
    verdict = compose_verdict(root.name, scenario.label, phases=[dict(phase='replay', seconds=round(time.monotonic() - started, 3))],
                              checks=checks, limits=limits, error='; '.join(errors) if errors else None,
                              evidence_class='HOST_REPLAY_OF_RETAINED_NATIVE_TRACES')
    verdict.update(input_class='NONE', commands_not_executed=executed, timeline=timeline)
    return verdict


def replay_suite(manifest, repo=REPO, evidence=None):
    """Replay every listed (scenario, retained trial) pair and compare with its expected outcome.

    Scenarios are read from `repo`; retained trials from `evidence` (default `repo`), since raw
    evidence lives only in the checkout that ran the trials.
    """
    repo, results = Path(repo), []
    evidence = Path(evidence) if evidence else repo
    for entry in read_json(manifest):
        if not (evidence / entry['trial']).is_dir():
            results.append(dict(scenario=entry['scenario'], trial=entry['trial'], expect=entry['expect'], outcome='MISSING',
                                error='retained trial not available here', failed_checks=[], checks=0, seconds=0.0,
                                as_expected=False))
            continue
        verdict = replay_trial(load_scenario(repo / entry['scenario'], repo=repo, require_artifact=False, variant=entry.get('variant')),
                               evidence / entry['trial'])
        reason = ' '.join([verdict['error'] or '', *verdict['failed_checks']])
        results.append(dict(scenario=entry['scenario'], trial=entry['trial'], expect=entry['expect'],
                            outcome=verdict['outcome'], error=verdict['error'], failed_checks=verdict['failed_checks'],
                            checks=len(verdict['checks']), seconds=verdict['phases'][0]['seconds'],
                            as_expected=verdict['outcome'] == entry['expect'] and entry.get('because', '') in reason))
    return results


def render_record(root):
    """The trial's written record, generated from its verdict and closed evidence. No machine paths."""
    root = Path(root)
    verdict, closed = read_json(root / 'verdict.json'), Closed(root)
    passed = [c['label'] for c in verdict['checks'] if c['passed']]
    lines = [f"# {verdict['trial']} - {verdict['scenario']}", '',
             f"**Outcome: {verdict['outcome']}** ({len(passed)} of {len(verdict['checks'])} checks passed). "
             f"Evidence {verdict['evidence_class']}; input {verdict['input_class']}; "
             f"visual review {verdict['visual_review']}; milestone acceptance {verdict['milestone_acceptance']}.", '']
    if verdict['error']:
        lines += [f"Error: {verdict['error']}", '']
    lines += ['## Identity', '']
    if (root / 'source-head.txt').exists():
        frozen = len(read_json(root / 'source-hashes.json')) if (root / 'source-hashes.json').exists() else 0
        lines.append(f"- Source commit `{(root / 'source-head.txt').read_text(encoding='utf-8').strip()}`; {frozen} frozen files")
    for role in LAUNCH_ORDER:
        start = closed.rows(role)[0] if closed.rows(role) else {}
        path = closed.trace_path(role)
        lines.append(f"- {role}: bridge {start.get('bridge_version')}, SDK `{start.get('sdk_commit')}`, executable "
                     f"`{start.get('executable_sha256')}`, {len(closed.rows(role))} trace rows"
                     + (f", trace SHA-256 `{sha256(path)}`" if path else ''))
    lines += ['', '## Phases', '', '| Phase | Seconds |', '|---|---|']
    lines += [f"| {p['phase']} | {p['seconds']:.1f} |" for p in verdict['phases']]
    lines += [f"| total | {sum(p['seconds'] for p in verdict['phases']):.1f} |", '']
    if verdict['failed_checks']:
        lines += ['## Failed checks', ''] + [f'- {label}' for label in verdict['failed_checks']] + ['']
    lines += [f'## Passed checks ({len(passed)})', ''] + [f'- {label}' for label in passed] + ['']
    for seen in verdict.get('observations', []):
        lines += [f"Observation ({seen['status']}): {seen['label']}: {', '.join(seen['roles'])}. {seen['note']}", '']
    if verdict['limits']:
        lines += ['## Not covered', ''] + [f"- {l['label']}: {l['status']}; prerequisite: {l['prerequisite']}"
                                           for l in verdict['limits']] + ['']
    if (root / 'window-placement.json').exists():
        placement = read_json(root / 'window-placement.json')
        lines += ['## Windows', '', f"- Parking display: {placement.get('display')}",
                  f"- A game held the foreground when checked: {placement.get('game_holds_foreground')}"]
        lines += [f"- {role}: first visible at {w.get('first_visible_at')}, on the parking display {w.get('opened_on_display')}, "
                  f"held the foreground when first seen {w.get('held_foreground')} "
                  f"(user idle {w.get('user_idle_seconds')} s)"
                  for role, w in placement.get('windows', {}).items()] + ['']
    return '\n'.join(lines)


def close_trial(root, *, run, alive, clock=time.monotonic, sleep=time.sleep):
    """Shut the launched workers down through their own message loops and retain their evidence.

    The first caller owns closure (runner or watchdog); a later caller gets None.
    """
    root = Path(root)
    try:
        (root / 'close.claim').open('x').close()
    except FileExistsError:
        return None
    workers = {role: read_json(root / (role + '.json')) for role in LAUNCH_ORDER if (root / (role + '.json')).exists()}
    failures = []

    def finish():
        result = dict(completed_utc=datetime.now(timezone.utc).isoformat(), clean=not failures, failures=failures)
        (root / 'closure-result.json').write_text(json.dumps(result, indent=1) + '\n', encoding='utf-8')
        return result

    def logged(command, log):
        code, stdout, stderr = run(command)
        (root / log).write_text(stdout + stderr, encoding='utf-8')
        return code, stdout

    for role, worker in workers.items():
        if not alive(worker['supervisor_pid']):
            continue
        prefix = [str(CONTROL), worker['generation'], str(worker['supervisor_pid'])]
        # A game that is still starting cannot take a shutdown yet (trial251 left one running): ask again.
        cutoff = clock() + STARTING_SECONDS
        while True:
            code, stdout, _ = run(prefix + ['status'])
            try:
                epoch = json.loads(stdout)['epoch'] if code == 0 else None
            except (json.JSONDecodeError, KeyError):
                epoch = None
            if epoch is not None and not logged(prefix + ['shutdown', str(epoch)], f'shutdown-{role}.log')[0]:
                break
            if not alive(worker['supervisor_pid']):
                break
            if clock() >= cutoff:
                failures.append(f'Shutdown rejected: {role}')
                break
            sleep(2)
    cutoff = clock() + SHUTDOWN_SECONDS
    while clock() < cutoff and any(alive(w['supervisor_pid']) for w in workers.values()):
        sleep(0.3)
    stuck = [role for role, w in workers.items() if alive(w['supervisor_pid'])]
    if stuck:
        failures.append('Supervisors still running: ' + ', '.join(stuck))
        return finish()
    plan = read_json(root / 'plan.json') if (root / 'plan.json').exists() else {}
    if plan.get('server_private') and Path(plan['server_private']).is_dir():
        (Path(plan['server_private']) / 'stop.request').write_text('stop', encoding='utf-8')
    python = [sys.executable, '-B']
    for role, worker in workers.items():
        run_directory = Path(worker['run'])
        host = read_rows(run_directory / 'native-host.jsonl') if (run_directory / 'native-host.jsonl').exists() else []
        exits = [r for r in host if r['event'] in ('game_exited', 'worker_exited')]
        if len(exits) != 2 or any(r['exit_code'] for r in exits):
            failures.append(f'Non-clean exit: {role}')
        shutil.copytree(run_directory, root / ('closed-' + role))
        profile = f'C:/Users/SporeMP-M04-{worker["worker_id"]}'
        backup = root / f'worker{worker["worker_id"]}-after'
        steps = (
            (python + [str(REPO / 'tools/native/worker-personal.py'), 'after', '--before',
                       str(run_directory / 'personal-before.json'), '--output', str(root / f'personal-after-{role}.json')],
             f'personal-after-{role}.log', 'Personal file check failed'),
            (python + [str(REPO / 'tools/diagnostics/sporemp_diag.py'), 'backup', '--source', profile + '/AppData/Roaming/Spore',
                       '--source', profile + '/Documents/My Spore Creations', '--destination', str(backup)],
             f'backup-{role}.json', 'Profile backup failed'),
            (python + [str(REPO / 'tools/diagnostics/sporemp_diag.py'), 'verify-backup', '--destination', str(backup)],
             f'verify-{role}.json', 'Profile backup verification failed'))
        for command, log, problem in steps:
            if logged(command, log)[0]:
                failures.append(f'{problem}: {role}')
    return finish()


def run_trial(scenario, root, ops, *, clock=time.monotonic, sleep=time.sleep):
    """One complete trial. Whatever fails, started processes are closed and the fixture is put back."""
    root = Path(root)
    root.mkdir(parents=True)
    phases, errors, checks, limits, input_classes = [], [], [], [], set()
    journal = root / 'fixture-restore' / 'restore.json'
    restored = display_requested = server_started = launched = False
    roles = {}

    def phase(name, action):
        started = clock()
        utc = datetime.now(timezone.utc).isoformat()
        try:
            return action()
        finally:
            phases.append(dict(phase=name, utc=utc, seconds=round(clock() - started, 3)))

    def guarded(name, action):
        try:
            return phase(name, action)
        except Exception as error:  # closure must continue past any single failed step
            errors.append(f'{name}: {type(error).__name__}: {error}')

    try:
        phase('preflight', ops.preflight)
        if scenario.artifact:
            phase('fixture_restore', lambda: ops.restore_fixture(scenario.artifact, journal.parent))
            restored = True
        phase('freeze', lambda: ops.freeze(root, scenario))
        # The display is added before any game exists: a topology change under a live game broke trial136.
        display_requested = True
        display = phase('display', lambda: ops.hold_display(root))
        private = phase('server', lambda: ops.start_server(root, root.name))
        server_started = True
        ops.configure_authority(private, scenario.authority)
        deadline = clock() + scenario.deadline_seconds
        (root / 'plan.json').write_text(json.dumps(dict(
            trial=root.name, scenario=scenario.name, deadline_seconds=scenario.deadline_seconds,
            server_private=str(private),
            deadline_utc=(datetime.now(timezone.utc) + timedelta(seconds=scenario.deadline_seconds)).isoformat())) + '\n',
            encoding='utf-8')
        # Started before the first game so an interrupted runner still ends in a normal closure.
        ops.start_watchdog(root)

        def launch():
            nonlocal launched
            for role in LAUNCH_ORDER:
                launched = True
                roles[role] = ops.start_worker(root, role, private)
                (root / (role + '.json')).write_text(json.dumps(roles[role], indent=1) + '\n', encoding='utf-8')
        phase('workers', launch)
        phase('park', lambda: ops.park(root, roles, display))
        phase('captures', lambda: ops.start_captures(root, roles, scenario.deadline_seconds))
        live = Live(root, roles, deadline, control=ops.control, clock=clock, sleep=sleep)
        try:
            phase('drive', lambda: scenario.drive(live))
        finally:
            checks.extend(live.checks)
            limits.extend(live.limits)
            input_classes.update(live.input_classes)
            live.close()
    except (ScenarioFailed, TrialAborted) as error:
        errors.append(str(error))
    except Exception as error:
        errors.append(f'{type(error).__name__}: {error}')

    if launched:
        guarded('close', lambda: ops.close(root))
    if server_started:
        guarded('server_stop', lambda: ops.stop_server(root, root.name))
    if launched:
        guarded('captures_finish', lambda: ops.finish_captures(root, roles))
    if display_requested:
        guarded('display_release', lambda: ops.release_display(root))
    # A scenario that saved its world can have that save sealed as a new fixture: after the games have closed
    # and before the worker profiles are put back, and only when the scenario itself did not fail.
    if launched and getattr(ops, 'sealing', False) and not errors and not ops.games_running():
        guarded('seal', lambda: ops.seal(root))
    if restored:
        if ops.games_running():
            errors.append('MANUAL_RECOVERY_REQUIRED: a game is still running, so the fixture was not undone')
        else:
            guarded('fixture_undo', lambda: ops.undo_fixture(journal))
    if launched:
        def review():
            checks.extend(standard_review(root, LAUNCH_ORDER, fixture=bool(scenario.artifact)))
            if scenario.review:
                closed = Closed(root)
                scenario.review(closed)
                checks.extend(closed.checks)
        guarded('review', review)
    verdict = compose_verdict(root.name, scenario.label, phases=phases, checks=checks, limits=limits,
                              error='; '.join(errors) if errors else None)
    verdict['observations'] = observations(root)
    verdict['input_class'] = '+'.join(sorted(input_classes)) or 'NONE'
    (root / 'verdict.json').write_text(json.dumps(verdict, indent=1, allow_nan=False) + '\n', encoding='utf-8')
    (root / 'RECORD.md').write_text(render_record(root) + '\n', encoding='utf-8')
    return verdict


def patch_config(text, overrides):
    """Apply a scenario's authority settings to a freshly generated session file."""
    lines = text.splitlines()
    for key, value in overrides.items():
        if key in PROTECTED_CONFIG:
            raise ValueError(f'Scenario may not override {key}')
        matches = [i for i, line in enumerate(lines) if line.split('=', 1)[0] == key]
        if len(matches) > 1:
            raise ValueError(f'Ambiguous {key} in generated configuration')
        if matches:
            lines[matches[0]] = f'{key}={value}'
        else:
            lines.append(f'{key}={value}')
    return '\n'.join(lines) + '\n'


def park_positions(display, sizes):
    """Cascade window outer sizes inside a display rectangle (left, top, right, bottom)."""
    left, top, right, bottom = display
    positions = []
    for index, (width, height) in enumerate(sizes):
        spare_x, spare_y = right - left - width, bottom - top - height
        if spare_x < 0 or spare_y < 0:
            raise ValueError(f'Window {width}x{height} does not fit the parking display')
        steps = max(len(sizes) - 1, 1)
        positions.append((left + spare_x * index // steps, top + spare_y * index // steps))
    return positions


def place_as_found(roles, find, place, *, clock=time.monotonic, sleep=time.sleep, timeout):
    """Call place(role, found) in the same poll that find(role) first succeeds; return what was found."""
    found = {}
    cutoff = clock() + timeout
    while len(found) < len(roles):
        for role in roles:
            if role not in found:
                result = find(role)
                if result:
                    found[role] = result
                    place(role, result)
        if len(found) < len(roles):
            if clock() >= cutoff:
                raise TrialAborted('No original game window within %ss: %s' % (
                    timeout, ', '.join(r for r in roles if r not in found)))
            sleep(WINDOW_POLL_SECONDS)
    return found


def worker_window_origin(display, role):
    """Client-area origin for a role's game window on the parking display, as the launch host passes it on."""
    x, y = park_positions(display, [GAME_OUTER] * len(LAUNCH_ORDER))[LAUNCH_ORDER.index(role)]
    return x + GAME_FRAME[0], y + GAME_FRAME[1]


def usable_session(info, alive, monitors):
    """The display rectangle of a session-long virtual display that is still really there, else None."""
    if info.get('state') != 'held' or not alive(info.get('pid', 0)):
        return None
    wanted = info.get('monitor', {})
    for monitor in monitors:
        if monitor['device'] == wanted.get('device') and list(monitor['rect']) == list(wanted.get('rect', [])):
            return tuple(monitor['rect'])
    return None


def session_display():
    info = SESSION_DISPLAY / 'session.json'
    if not info.exists():
        return None
    code, stdout, _ = run_command([sys.executable, '-B', str(REPO / 'tools/native/virtual-display.py'), 'monitors'], 30)
    return usable_session(read_json(info), process_alive, json.loads(stdout) if code == 0 else [])


def wait_for_progress(roles, sample, *, clock=time.monotonic, sleep=time.sleep, timeout):
    """Wait until every role's update counter has advanced; return {role: (first, last)}.

    sample(role) returns the counter or None while it cannot be read yet. A game that starts
    later than the others (the authority is launched last) is given time instead of one fixed gap.
    """
    first, last = {}, {}
    cutoff = clock() + timeout
    while True:
        for role in roles:
            value = sample(role)
            if value is not None:
                first.setdefault(role, value)
                last[role] = value
        waiting = [role for role in roles if role not in first or last[role] <= first[role]]
        if not waiting:
            return {role: (first[role], last[role]) for role in roles}
        if clock() >= cutoff:
            raise TrialAborted('No app updates after launch: ' + ', '.join(waiting))
        sleep(LIVENESS_POLL_SECONDS)


def trial_name(value):
    """start-m06-server.ps1 uses the trial name as its run name."""
    if not re.fullmatch(r'[a-z0-9-]{1,48}', value):
        raise ValueError('Trial name must be lowercase letters, digits and hyphens')
    return value


def frozen_paths(changed, untracked, scenario):
    """Everything that decides what a trial ran: edits, the scenario, the tooling and the launched binaries."""
    return sorted({Path(p).as_posix() for p in (*changed, *untracked, scenario, *TOOLS, *BINARIES)})


def filmstrip_filter(duration):
    if not duration > 0:
        raise ValueError('Capture has no duration')
    return f'fps=16/{duration:.3f},scale=320:180,tile=4x4'


def process_alive(pid):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.restype = ctypes.c_void_p
    handle = kernel.OpenProcess(0x1000, False, int(pid))  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        # Workers run under other accounts: access denied still proves the process exists.
        return ctypes.get_last_error() == 5
    try:
        code = ctypes.c_uint32()
        return bool(kernel.GetExitCodeProcess(ctypes.c_void_p(handle), ctypes.byref(code))) and code.value == 259
    finally:
        kernel.CloseHandle(ctypes.c_void_p(handle))


def run_command(command, timeout=120):
    """(exit code, stdout, stderr). A timeout is reported, never retried.

    Output goes to files, not pipes: the launch scripts leave a coordinator or
    supervisor running that inherits the handles, and a pipe would never close.
    """
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        process = subprocess.Popen(command, cwd=REPO, stdin=subprocess.DEVNULL, stdout=out, stderr=err)
        try:
            code = process.wait(timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise TimeoutError(' '.join(map(str, command[:4])))
        out.seek(0)
        err.seek(0)
        return code, out.read().decode('utf-8', 'replace'), err.read().decode('utf-8', 'replace')


ORPHAN_LAUNCHER = '''
import json, subprocess, sys
flags = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP
with open(sys.argv[2], "ab") as log:
    for extra in (subprocess.CREATE_BREAKAWAY_FROM_JOB, 0):
        try:
            child = subprocess.Popen(json.loads(sys.argv[1]), cwd=sys.argv[3], stdin=subprocess.DEVNULL,
                                     stdout=log, stderr=subprocess.STDOUT, creationflags=flags | extra)
            break
        except OSError:
            if not extra:
                raise
print(child.pid)
'''


def spawn_orphan(command, log):
    """Start a helper that survives this runner being killed with its whole process tree; return its pid.

    A short-lived launcher starts the helper and exits, so the helper has no living parent to be
    reached through. The deadline watchdog and the display holder must not die with the runner:
    in trial157 an interrupted runner took its watchdog down and left three games running.
    """
    code, stdout, stderr = run_command([sys.executable, '-c', ORPHAN_LAUNCHER, json.dumps(list(map(str, command))),
                                        str(log), str(REPO)], 30)
    if code or not stdout.strip().isdigit():
        raise RuntimeError('Could not start a detached helper: ' + stderr.strip()[-300:])
    return int(stdout)


def spawn_detached(command, log):
    """A helper that may end with this runner (the window recorders)."""
    with Path(log).open('ab') as stream:
        return subprocess.Popen(command, cwd=REPO, stdin=subprocess.DEVNULL, stdout=stream, stderr=subprocess.STDOUT,
                                creationflags=subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP)


def game_window(pid):
    """The original game's visible top-level window for one process, or None."""
    user = ctypes.WinDLL('user32', use_last_error=True)
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)
    def visit(window, _data):
        owner = ctypes.c_uint32()
        user.GetWindowThreadProcessId(ctypes.c_void_p(window), ctypes.byref(owner))
        if owner.value == pid and user.IsWindowVisible(ctypes.c_void_p(window)):
            title = ctypes.create_unicode_buffer(128)
            user.GetWindowTextW(ctypes.c_void_p(window), title, 128)
            if title.value == GAME_WINDOW_TITLE:
                found.append(window)
        return 1
    user.EnumWindows(visit, None)
    return found[0] if len(found) == 1 else None


def user_idle_seconds():
    """Seconds since the last keyboard or mouse input in this session. Reads only; sends nothing."""
    class LastInput(ctypes.Structure):
        _fields_ = [('size', ctypes.c_uint32), ('tick', ctypes.c_uint32)]
    info = LastInput(); info.size = ctypes.sizeof(LastInput)
    if not ctypes.WinDLL('user32').GetLastInputInfo(ctypes.byref(info)):
        return None
    return round(((ctypes.WinDLL('kernel32').GetTickCount() - info.tick) & 0xFFFFFFFF) / 1000, 1)


def window_rect(window):
    rect = (ctypes.c_long * 4)()
    ctypes.WinDLL('user32').GetWindowRect(ctypes.c_void_p(window), rect)
    return list(rect)


class NativeOps:
    """The real effects behind run_trial: the existing guarded scripts, plus window placement."""

    def __init__(self, port, display, capture=True, sound=False, sealing=False):
        self.port, self.display, self.capture, self.sound, self.sealing = port, display, capture, sound, sealing
        self.windows = {}
        self.parking = None
        self.session = False

    def _checked(self, command, log, timeout=120):
        code, stdout, stderr = run_command(command, timeout)
        Path(log).write_text(stdout + stderr, encoding='utf-8')
        if code:
            raise RuntimeError(f'{Path(command[3] if command[0] == "pwsh" else command[2]).name} exited {code}: '
                               + (stdout + stderr).strip()[-400:])
        return stdout

    def games_running(self):
        return any(line.startswith('SporeApp.exe') for line in run_command(
            ['tasklist', '/FI', 'IMAGENAME eq SporeApp.exe', '/FO', 'CSV', '/NH'])[1].replace('"', '').splitlines())

    def preflight(self):
        missing = [p for p in (*BINARIES, *TOOLS) if not (REPO / p).is_file()]
        if missing:
            raise RuntimeError('Missing build output or tool: ' + ', '.join(missing))
        if self.games_running():
            raise RuntimeError('An original SPORE process is already running')
        for worker, _ in ROLE_WORKERS.values():
            if not read_json(REPO / f'local/worker-accounts/{worker}/account.json').get('prepared'):
                raise RuntimeError(f'Worker {worker} is not prepared')
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', self.port))
        if self.capture and not shutil.which('ffmpeg'):
            raise RuntimeError('ffmpeg is required for captures')

    def restore_fixture(self, artifact, output):
        self._checked([sys.executable, '-B', str(REPO / 'tools/native/durable-checkpoint.py'), 'restore', '--artifact',
                       str(artifact), '--workers', *sorted(w for w, _ in ROLE_WORKERS.values()), '--output', str(output)],
                      output.parent / 'fixture-restore.log')

    def seal(self, root):
        """Seal the authority's closed save as `sealed-checkpoint` in the trial folder (M09 sealing, unchanged)."""
        self._checked([sys.executable, '-B', str(REPO / 'tools/native/durable-checkpoint.py'), 'seal', '--worker',
                       ROLE_WORKERS['authority'][0], '--config', str(root / 'closed-authority/network-session.cfg'),
                       '--output', str(root / 'sealed-checkpoint')], root / 'seal.log', timeout=300)

    def undo_fixture(self, journal):
        self._checked([sys.executable, '-B', str(REPO / 'tools/native/durable-checkpoint.py'), 'undo', '--journal',
                       str(journal)], journal.parent.parent / 'fixture-undo.log')

    def freeze(self, root, scenario):
        def git(*arguments):
            code, stdout, stderr = run_command(['git', *arguments])
            if code:
                raise RuntimeError('Source inventory failed: ' + stderr.strip())
            return [line for line in stdout.splitlines() if line.strip()]
        (root / 'source-head.txt').write_text(git('rev-parse', 'HEAD')[0] + '\n', encoding='utf-8')
        freeze_sources(REPO, root, frozen_paths(
            # A deleted file has nothing to freeze.
            [p for p in git('diff', '--name-only', 'HEAD', '--', 'src', 'tests', 'tools', 'CMakeLists.txt')
             if (REPO / p).is_file()],
            git('ls-files', '--others', '--exclude-standard', '--', 'src', 'tests', 'tools'),
            scenario.path.relative_to(REPO)))

    def hold_display(self, root):
        if self.display == 'none':
            return None
        info = root / 'virtual-display.json'
        self.parking = session_display()
        if self.parking:
            # Reused, so no display is added or removed around this trial.
            self.session = True
            info.write_text(json.dumps(dict(state='session', rect=list(self.parking))) + '\n', encoding='utf-8')
            return self.parking
        spawn_orphan([sys.executable, '-B', str(REPO / 'tools/native/virtual-display.py'), 'hold', '--info', str(info),
                        '--stop', str(root / 'display.stop'), '--max-seconds', str(MAX_DEADLINE_SECONDS + 180)],
                       root / 'virtual-display.log')
        cutoff = time.monotonic() + 10
        while time.monotonic() < cutoff:
            if info.exists():
                state = read_json(info)
                if state['state'] == 'held':
                    self.parking = tuple(state['monitor']['rect'])
                    return self.parking
                raise RuntimeError('Virtual display unavailable: ' + state['state'])
            time.sleep(0.1)
        raise RuntimeError('Virtual display holder did not report')

    def release_display(self, root):
        if self.display == 'none' or self.session:
            return
        (root / 'display.stop').touch()
        info, cutoff = root / 'virtual-display.json', time.monotonic() + 5
        while time.monotonic() < cutoff and info.exists() and read_json(info)['state'] == 'held':
            time.sleep(0.1)

    def start_server(self, root, name):
        self._checked(['pwsh', '-NoProfile', '-File', str(REPO / 'tools/native/start-m06-server.ps1'),
                       '-RunName', name, '-Port', str(self.port)], root / 'start-server.log')
        return REPO / 'local/m06-network' / name / 'private'

    def configure_authority(self, private, overrides):
        if overrides:
            config = Path(private) / 'authority.conf'
            config.write_bytes(patch_config(config.read_text(encoding='utf-8'), overrides).encode('utf-8'))

    def stop_server(self, root, name):
        private = REPO / 'local/m06-network' / name / 'private'
        if private.is_dir() and not (private / 'stop.request').exists():
            (private / 'stop.request').write_text('stop', encoding='utf-8')

    def start_watchdog(self, root):
        spawn_orphan([sys.executable, '-B', str(Path(__file__).resolve()), 'watchdog', '--trial', str(root)],
                       root / 'watchdog.log')

    def start_worker(self, root, role, private):
        worker, bridge_role = ROLE_WORKERS[role]
        self._checked(['pwsh', '-NoProfile', '-File', str(REPO / 'tools/native/start-worker.ps1'), '-WorkerId', worker,
                       '-Generation', uuid.uuid4().hex, '-Desktop', 'current', '-M05Role', bridge_role,
                       '-M06SessionConfig', str(Path(private) / (role + '.conf')), *self.presentation(role)],
                      root / f'start-{role}.log')
        return read_json(REPO / f'local/worker-accounts/{worker}/current.json')

    def presentation(self, role):
        """Launch-host options: the bridge opens the window on the parking display; the game starts without audio."""
        options = []
        if self.parking:
            options += ['-WindowPosition', '%d,%d' % worker_window_origin(self.parking, role)]
        if not self.sound:
            options.append('-NoSound')
        return options

    def control(self, command):
        return run_command(command, timeout=12)

    def park(self, root, roles, display):
        """Find the three game windows, record where each first appeared, and check the games are simulating."""
        user = ctypes.WinDLL('user32', use_last_error=True)
        user.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
        user.GetForegroundWindow.restype = ctypes.c_void_p
        record = dict(display=list(display) if display else None, placement='bridge' if display else 'none',
                      sound=self.sound, windows={})

        def find(role):
            meta = roles[role]
            if not process_alive(meta['supervisor_pid']):
                raise TrialAborted(f'{role}: supervisor exited before its game window appeared')
            try:
                pid = read_json(Path(meta['run']) / 'worker-status.json').get('game_pid')
            except (OSError, json.JSONDecodeError):
                return None  # not written yet, or being rewritten
            window = game_window(pid) if pid else None
            return (pid, window) if window else None

        def seen(role, found):
            pid, window = found
            rect = window_rect(window)
            # Windows hands the foreground to a new program more readily when the user is idle,
            # so the idle time is recorded next to whether this game took it.
            entry = record['windows'][role] = dict(pid=pid, window=window, first_visible_at=rect,
                                                   held_foreground=user.GetForegroundWindow() == window,
                                                   user_idle_seconds=user_idle_seconds())
            if display:
                entry['opened_on_display'] = display[0] <= rect[0] < display[2] and display[1] <= rect[1] < display[3]

        try:
            self.windows = place_as_found(list(roles), find, seen, timeout=WINDOW_SECONDS)
            # A parked game must still be simulating: its app update counter has to advance.
            def updates(role):
                meta = roles[role]
                code, stdout, _ = run_command([str(CONTROL), meta['generation'], str(meta['supervisor_pid']), 'status'], 12)
                return json.loads(stdout)['app_updates'] if code == 0 else None
            try:
                samples = wait_for_progress(list(roles), updates, timeout=LIVENESS_SECONDS)
            finally:
                record['liveness_seconds_allowed'] = LIVENESS_SECONDS
            record['app_updates'] = samples
            for role, (_, window) in self.windows.items():
                record['windows'][role].update(rect=window_rect(window), minimized=bool(user.IsIconic(ctypes.c_void_p(window))))
            record['game_holds_foreground'] = user.GetForegroundWindow() in [w for _, w in self.windows.values()]
        finally:
            (root / 'window-placement.json').write_text(json.dumps(record, indent=1) + '\n', encoding='utf-8')

    def start_captures(self, root, roles, seconds):
        if not self.capture:
            return
        for role in roles:
            pid, _ = self.windows[role]
            spawn_detached(['pwsh', '-NoProfile', '-File', str(REPO / 'tools/native/capture-game.ps1'), '-GamePid', str(pid),
                            '-OutputDirectory', str(root / ('capture-' + role)), '-Seconds', str(seconds),
                            '-AllowConcurrent'], root / f'capture-{role}.log')

    def finish_captures(self, root, roles):
        if not self.capture:
            return
        cutoff = time.monotonic() + 40
        captures = [root / ('capture-' + role) for role in roles if role in self.windows]
        while time.monotonic() < cutoff and not all((c / 'completion.json').exists() for c in captures):
            time.sleep(0.5)
        for capture in captures:
            video = capture / 'capture.mkv'
            if not video.is_file():
                continue
            code, stdout, _ = run_command(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of',
                                           'csv=p=0', str(video)], 60)
            if code:
                continue
            (capture / 'duration.txt').write_text(stdout.strip() + '\n', encoding='utf-8')
            run_command(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-n', '-i', str(video), '-vf',
                         filmstrip_filter(float(stdout)), '-frames:v', '1', str(capture / 'filmstrip.png')], 120)

    def close(self, root):
        result = close_trial(root, run=lambda command: run_command(command, 180), alive=process_alive)
        cutoff = time.monotonic() + 120
        while result is None and time.monotonic() < cutoff:  # the watchdog owns closure; wait for it
            if (root / 'closure-result.json').exists():
                result = read_json(root / 'closure-result.json')
            else:
                time.sleep(0.5)
        if result is None:
            raise RuntimeError('Closure was claimed elsewhere and did not complete')
        return result


def watchdog(root):
    """Close the trial at its deadline unless the runner already did."""
    root = Path(root)
    deadline = datetime.fromisoformat(read_json(root / 'plan.json')['deadline_utc'])
    while datetime.now(timezone.utc) < deadline:
        if (root / 'close.claim').exists():
            return 0
        time.sleep(0.25)
    close_trial(root, run=lambda command: run_command(command, 180), alive=process_alive)
    return 0


def display_command(action, hours):
    info, stop = SESSION_DISPLAY / 'session.json', SESSION_DISPLAY / 'stop'
    current = session_display()
    if action == 'start' and not current:
        SESSION_DISPLAY.mkdir(parents=True, exist_ok=True)
        for stale in (info, stop):
            stale.unlink(missing_ok=True)
        spawn_orphan([sys.executable, '-B', str(REPO / 'tools/native/virtual-display.py'), 'hold', '--info', str(info),
                        '--stop', str(stop), '--max-seconds', str(int(min(max(hours, 0.01), 12) * 3600))],
                       SESSION_DISPLAY / 'holder.log')
        cutoff = time.monotonic() + 10
        while time.monotonic() < cutoff and not current:
            time.sleep(0.2)
            current = session_display()
    elif action == 'stop' and info.exists():
        stop.touch()
        cutoff = time.monotonic() + 6
        while time.monotonic() < cutoff and read_json(info).get('state') == 'held' and process_alive(read_json(info).get('pid', 0)):
            time.sleep(0.2)
        current = session_display()
    print(json.dumps(dict(action=action, session_display=list(current) if current else None)))
    return 0 if (current is None) == (action == 'stop') or action == 'status' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    run = sub.add_parser('run', help='launch, drive, close and review one trial')
    run.add_argument('--scenario', type=Path, required=True)
    run.add_argument('--variant', help="a name from the scenario file's VARIANTS")
    run.add_argument('--name', type=trial_name, required=True)
    run.add_argument('--trials', type=Path, default=REPO / 'local/native-trials')
    run.add_argument('--port', type=int, default=27200, choices=range(1024, 65536), metavar='1024-65535')
    run.add_argument('--display', choices=('virtual', 'none'), default='virtual',
                     help='park game windows on a temporary virtual display, or leave them where they open')
    run.add_argument('--no-capture', action='store_true')
    run.add_argument('--sound', action='store_true', help='let the games play audio (default: started with noSound)')
    run.add_argument('--seal', action='store_true',
                     help="seal the save the scenario requested as <trial>/sealed-checkpoint, for a later scenario's ARTIFACT")
    guard = sub.add_parser('watchdog', help='internal: deadline closure')
    guard.add_argument('--trial', type=Path, required=True)
    again = sub.add_parser('replay', help='check a scenario against one retained trial; no game')
    again.add_argument('--scenario', type=Path, required=True)
    again.add_argument('--variant', help="a name from the scenario file's VARIANTS")
    again.add_argument('--trial', type=Path, required=True)
    suite = sub.add_parser('replay-suite', help='replay every pair in a manifest and compare with its expected outcome')
    suite.add_argument('--manifest', type=Path, required=True)
    suite.add_argument('--evidence-root', type=Path, help='checkout that holds the retained trials (default: this one)')
    record = sub.add_parser('record', help='print the generated record of a closed trial')
    record.add_argument('--trial', type=Path, required=True)
    shown = sub.add_parser('display', help='hold one virtual display for a whole work session')
    shown.add_argument('action', choices=('start', 'stop', 'status'))
    shown.add_argument('--hours', type=float, default=8, help='the holder ends by itself after this long (at most 12)')
    args = parser.parse_args()
    if args.command == 'display':
        return display_command(args.action, args.hours)
    if args.command == 'watchdog':
        return watchdog(args.trial)
    if args.command == 'record':
        print(render_record(args.trial))
        return 0
    if args.command == 'replay':
        verdict = replay_trial(load_scenario(args.scenario, require_artifact=False, variant=args.variant), args.trial)
        print(json.dumps({k: verdict[k] for k in ('trial', 'outcome', 'error', 'failed_checks', 'evidence_class',
                                                  'commands_not_executed')} | dict(checks=len(verdict['checks']))))
        return 0 if verdict['outcome'] == 'PASS' else 1
    if args.command == 'replay-suite':
        results = replay_suite(args.manifest, evidence=args.evidence_root)
        for result in results:
            print('%-4s %s  %-4s (expected %s)  %3d checks  %5.2fs  %s  %s' % (
                'ok' if result['as_expected'] else 'DIFF', Path(result['trial']).name, result['outcome'], result['expect'],
                result['checks'], result['seconds'], Path(result['scenario']).stem, (result['error'] or '')[:70]))
        different = sum(not r['as_expected'] for r in results)
        print(json.dumps(dict(pairs=len(results), as_expected=len(results) - different, different=different,
                              evidence_class='HOST_REPLAY_OF_RETAINED_NATIVE_TRACES')))
        return 1 if different else 0
    trials = args.trials.resolve()
    if not trials.is_relative_to(REPO / 'local'):
        parser.error('Raw trial evidence must stay under the ignored local/ directory')
    scenario = load_scenario(args.scenario, variant=args.variant)
    if not scenario.path.is_relative_to(REPO):
        parser.error('The scenario must be inside this repository so it can be frozen with the trial')
    with display_awake() as power:
        verdict = run_trial(scenario, trials / args.name, NativeOps(args.port, args.display, not args.no_capture, args.sound, args.seal))
    (trials / args.name / 'display-power.json').write_text(json.dumps(power, indent=1), encoding='utf-8')
    print(json.dumps(dict(trial=verdict['trial'], outcome=verdict['outcome'], error=verdict['error'],
                          failed_checks=verdict['failed_checks'],
                          seconds={p['phase']: p['seconds'] for p in verdict['phases']},
                          verdict=str(trials / args.name / 'verdict.json'))))
    return 0 if verdict['outcome'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
