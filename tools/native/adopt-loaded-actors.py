"""Adopt the already loaded, sealed M04 actors without a second load or spawn."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

repo=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('checkpoint',repo/'tools/native/worker-checkpoint.py')
checkpoint=importlib.util.module_from_spec(spec);spec.loader.exec_module(checkpoint)
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('worker_id', choices=('01', '02'))
    parser.add_argument('--sidecar', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    worker_id = args.worker_id
    output = checkpoint.service.diag.no_reparse(args.output.absolute())
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x',encoding='utf-8') as stream:
        report={'worker':worker_id,'adopted':False,'commands':[]}
        try:
            current=checkpoint.workers.current(worker_id)
            account,games=checkpoint.account_paths(worker_id)
            metadata=checkpoint.workers.read_json(checkpoint.service.diag.no_reparse(args.sidecar.absolute()))
            actors=checkpoint.validate_sidecar(metadata,worker_id,games,account)
            live=checkpoint.workers.control(worker_id,captured=current)
            if live['phase']!=3 or live['persistence_state']!=6 or live['actor_a'] or live['actor_b']:
                raise ValueError('Already loaded unbound original scene required')
            argv=[str(repo/'build/win32/Release/SporeMP.WorkerControl.exe'),current['generation'],str(current['supervisor_pid']),
                  'restore',str(live['epoch']),*map(str,checkpoint.pack_actors(actors))]
            result=subprocess.run(argv,capture_output=True,text=True,timeout=13,creationflags=subprocess.CREATE_NO_WINDOW)
            report['commands'].append({'argv':argv,'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
            reply=json.loads(result.stdout)
            if result.returncode or reply['result']!='accepted' or not reply['actor_a'] or not reply['actor_b']:
                raise ValueError('Original adoption failed')
            report.update(adopted=True,current=current,status=reply,sealed_games_sha256=metadata['games_sha256'])
        except Exception as error:report['error']=str(error)
        json.dump(report,stream,indent=2)
    print(json.dumps(report))
    raise SystemExit(0 if report['adopted'] else 1)


if __name__ == "__main__":
    main()
