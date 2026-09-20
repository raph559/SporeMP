"""Verify personal files before/after isolated workers, including a concurrent peer."""
import argparse
import json
import os
from pathlib import Path
import sys
from uuid import uuid4

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'tools/launcher'))
import launcher_service as service

from worker_manager import checked_games


def verify(report):
    backup = service.diag.no_reparse(Path(report['backup_directory']))
    service.diag.verify_backup(backup)
    manifest = json.loads((backup / 'backup-manifest.json').read_text())
    matches = [service.diag.tree_manifest(Path(source['source'])) == source['snapshot'] for source in manifest['sources']]
    if not all(matches): raise ValueError('Personal files changed; retain evidence and take a new closed backup.')
    return {'personal_sources_unchanged': True, 'backup_directory': str(backup),
            'file_count': sum(len(x['snapshot']['files']) for x in manifest['sources'])}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('before', 'after'))
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--before', type=Path)
    args = parser.parse_args()
    shared = service.diag.no_reparse(REPO / 'local/worker-accounts/personal-backup.json')
    games = checked_games()
    if args.action == 'before':
        if not games:
            report = service.ensure_backup()
            temporary = shared.with_name(uuid4().hex + '.tmp')
            service.diag.write_json(temporary, report)
            os.replace(temporary, shared)
        else:
            report = json.loads(shared.read_text())
        result = verify(report)
    else:
        if args.before is None: raise ValueError('The exact before report is required.')
        result = verify(json.loads(service.diag.no_reparse(args.before).read_text()))
    service.diag.write_json(args.output, {**result, 'utc': service.diag.utc_now(), 'recognized_running_workers': len(games)})
    print(json.dumps(result))

if __name__ == '__main__': main()
