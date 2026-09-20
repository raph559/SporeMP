"""Launcher controls for real local C++ supervisors and prepared isolated fixtures."""
import json
from pathlib import Path
import subprocess
import time
import ctypes
from ctypes import wintypes
from uuid import uuid4

REPO = Path(__file__).resolve().parents[2]

def read_json(path):
    from launcher_service import diag
    path = diag.no_reparse(path)
    if path.stat().st_size > 65536: raise ValueError('Worker metadata is too large')
    value = json.loads(path.read_text(encoding='utf-8-sig'))
    if not isinstance(value, dict) or value.get('schema_version') != 1:
        raise ValueError('Unsupported worker metadata')
    return value

def root_for(worker_id):
    if worker_id not in ('01', '02', '03'): raise ValueError('Choose a prepared worker')
    return REPO / 'local/worker-accounts' / worker_id

def current(worker_id):
    value = read_json(root_for(worker_id) / 'current.json')
    return validate_current(worker_id, value)


def validate_current(worker_id, value):
    root_for(worker_id)
    if not isinstance(value, dict): raise ValueError('Invalid worker metadata')
    nonce = value.get('generation', '')
    if not isinstance(nonce, str) or len(nonce) != 32 or any(c not in '0123456789abcdef' for c in nonce) or int(nonce, 16) == 0:
        raise ValueError('Invalid worker generation')
    if value.get('worker_id') != worker_id or type(value.get('supervisor_pid')) is not int or not 0 < value['supervisor_pid'] <= 0xffffffff:
        raise ValueError('Worker identity mismatch')
    expected = Path(f'C:/ProgramData/SporeMP/M04/{worker_id}/runs') / nonce
    if Path(value.get('run', '')) != expected: raise ValueError('Worker evidence path escaped its workspace')
    return value

def control(worker_id, operation='status', epoch=0, *, captured=None):
    # Multi-request operations retain their original process/generation even if
    # another operator replaces current.json before the second request.
    value = current(worker_id) if captured is None else validate_current(worker_id, captured)
    command = [str(REPO / 'build/win32/Release/SporeMP.WorkerControl.exe'), value['generation'],
               str(value['supervisor_pid']), operation]
    if operation != 'status': command.append(str(epoch))
    result = subprocess.run(command, capture_output=True, text=True, timeout=13,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode not in (0, 5): raise ValueError(result.stderr.strip() or 'The worker supervisor is unavailable')
    response = json.loads(result.stdout)
    if response.get('schema_version') != 1 or response.get('op') != operation:
        raise ValueError('Unexpected worker control response')
    return response

def supervisor_running(value):
    """Query the actual OS process; a stale status file is not live readiness."""
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    process = kernel.OpenProcess(0x101000, False, value['supervisor_pid'])
    if not process:
        if ctypes.get_last_error() == 87: return False  # PID no longer exists
        raise ValueError('Windows could not verify the worker process; restart is blocked')
    try:
        if kernel.WaitForSingleObject(process, 0) == 0: return False
        size = wintypes.DWORD(32768)
        path = ctypes.create_unicode_buffer(size.value)
        if not kernel.QueryFullProcessImageNameW(process, 0, path, ctypes.byref(size)):
            raise ValueError('Could not verify worker executable identity')
        return Path(path.value) == Path(value['payload']) / 'SporeMP.NativeHost.exe'
    finally: kernel.CloseHandle(process)

def worker_row(worker_id):
    root = root_for(worker_id)
    if not (root / 'account.json').exists(): return None
    account = read_json(root / 'account.json')
    row = {'id': worker_id, 'label': f'Worker {worker_id}', 'prepared': bool(account.get('prepared')),
           'state': 'stopped', 'detail': 'Prepared Creature fixture. Opens a SPORE window.', 'running': False,
           'can_start': bool(account.get('prepared')), 'can_stop': False}
    if (root / 'current.json').exists():
        value = current(worker_id)
        status_path = Path(value['run']) / 'worker-status.json'
        status = read_json(status_path) if status_path.exists() else {}
        if status and status.get('generation') != value['generation']: raise ValueError('Worker status generation mismatch')
        if not supervisor_running(value):
            row['state'] = status['state'] if status.get('state') in ('stopped', 'stopped_forced') else 'crashed'
            row['detail'] = ('Worker stopped.' if row['state'] == 'stopped' else
                             'Worker was stopped forcibly. No native checkpoint was confirmed; run logs are preserved.' if row['state'] == 'stopped_forced' else
                             'Worker exited unexpectedly. Its run logs are preserved.')
        else:
            row.update(running=True, can_start=False)
            try:
                live = control(worker_id, captured=value)
                fresh = status_path.exists() and time.time() - status_path.stat().st_mtime < 5
                row.update(state=status.get('state', 'starting') if fresh else 'unresponsive', can_stop=True,
                           detail=f"Engine updates: {live['app_updates']:,} · native AI calls: {live['native_ai_entries']:,}")
            except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
                row.update(state='unavailable', detail=str(error))
        row['run'] = value['run']
    return row


def worker_list():
    workers = []
    for worker_id in ('01', '02', '03'):
        try:
            row = worker_row(worker_id)
        except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
            # One unreadable peer must not hide a healthy worker or turn an
            # already acknowledged start/stop into an apparent failed mutation.
            row = {'id': worker_id, 'label': f'Worker {worker_id}', 'prepared': False,
                   'state': 'unavailable', 'detail': str(error), 'running': False,
                   'can_start': False, 'can_stop': False}
        if row is not None: workers.append(row)
    return {'workers': workers, 'desktop_requirement': 'Keep Windows signed in and worker windows open without minimizing them. Minimizing pauses the simulation and can cause the worker to time out.',
            'qualification': 'Experimental M04 worker controls; full native acceptance is pending.'}

def start(worker_id):
    from launcher_service import preparation_lock
    with preparation_lock():
        rows = worker_list()['workers']
        row = next((r for r in rows if r['id'] == worker_id), None)
        if not row or not row['can_start']: raise ValueError('This worker cannot start in its current state')
        result = subprocess.run(['pwsh', '-NoProfile', '-File', str(REPO / 'tools/native/start-worker.ps1'),
                                 '-WorkerId', worker_id, '-Generation', uuid4().hex, '-Desktop', 'current'],
                                capture_output=True, text=True, timeout=90, creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode: raise ValueError(result.stderr.strip() or result.stdout.strip() or 'Worker start failed')
        value = json.loads(result.stdout)
        return {'started': True, 'worker_id': worker_id, 'supervisor_pid': value['supervisor_pid'],
                'message': 'Worker started. Waiting for the original engine.', **worker_list()}

def stop(worker_id):
    value = current(worker_id)
    live = control(worker_id, captured=value)
    response = control(worker_id, 'shutdown', live['epoch'], captured=value)
    if response.get('result') != 'accepted':
        raise ValueError(f"Worker refused shutdown ({response.get('result', 'unknown')}). Refresh its status before trying again.")
    return {'worker_id': worker_id, 'generation': value['generation'], 'supervisor_pid': value['supervisor_pid'],
            'shutdown_result': response['result'],
            'message': 'Shutdown requested; waiting for the original engine to exit.', **worker_list()}


def os_games():
    command = "$ErrorActionPreference='Stop'; @(Get-CimInstance Win32_Process -Filter \"Name='SporeApp.exe'\" | ForEach-Object { $owner=Invoke-CimMethod -InputObject $_ -MethodName GetOwnerSid; @{pid=$_.ProcessId;parent=$_.ParentProcessId;path=$_.ExecutablePath;sid=$owner.Sid} }) | ConvertTo-Json -Compress"
    result = subprocess.run(['powershell', '-NoProfile', '-Command', command], capture_output=True, text=True,
                            check=True, timeout=5, creationflags=subprocess.CREATE_NO_WINDOW)
    games = json.loads(result.stdout or '[]')
    return [games] if isinstance(games, dict) else games


def checked_games():
    """Accept only current OS processes belonging to registered worker generations."""
    games = os_games()
    allowed = set()
    for worker_id in ('01', '02', '03'):
        root = root_for(worker_id)
        if not (root / 'current.json').exists(): continue
        account, value = read_json(root / 'account.json'), current(worker_id)
        if not account.get('prepared') or account.get('user') != f'SporeMP-M04-{worker_id}':
            raise ValueError('Worker account identity changed')
        status_path = Path(value['run']) / 'worker-status.json'
        if not status_path.exists() or not supervisor_running(value): continue
        status = read_json(status_path)
        if status.get('generation') != value['generation']: continue
        allowed.add((status['game_pid'], value['supervisor_pid'], account['sid'],
                     str(Path(account['game_root']) / 'SporebinEP1/SporeApp.exe').lower()))
    for game in games:
        if (game.get('pid'), game.get('parent'), game.get('sid'), (game.get('path') or '').lower()) not in allowed:
            raise ValueError('A personal or unrecognized SPORE process is running')
    return games


def only_registered_workers_running():
    try: return bool(checked_games())
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError): return False
