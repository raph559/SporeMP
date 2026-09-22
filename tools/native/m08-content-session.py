"""Bounded private editor transaction session. Never launches SPORE or desktop UI."""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import time
from uuid import uuid4

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/"tools/content"))
import spore_content as content
from content_session import ContentSession, execute, private, require


def publish(path, value):
    temporary = path.with_suffix(".tmp")
    content.write_new(temporary, content.canonical(value))
    os.rename(temporary, path)


def serve(args):
    output = private(args.output)
    output.mkdir(parents=True, exist_ok=False)
    (output/"requests").mkdir(); (output/"responses").mkdir()
    session = ContentSession(args.config, output/"client")
    content.write_new(output/"ready.json", content.canonical(dict(pid=os.getpid(), authenticated=session.authenticated,
        lifetime_seconds=args.seconds, native_gameplay_client=False)))
    deadline = time.monotonic()+args.seconds
    try:
        while time.monotonic() < deadline:
            for path in sorted((output/"requests").glob("*.json")):
                require(re.fullmatch("[a-f0-9]{32}\.json", path.name), "Invalid request filename")
                reply = output/"responses"/path.name
                if reply.exists():
                    continue
                raw = content.read_bounded(path, 4096)
                command = json.loads(raw)
                result = dict(request_sha256=content.digest(raw), operation=command.get("op"), mutation_retried=False)
                try:
                    if command == {"op": "close"}:
                        result["closed"] = True
                        publish(reply, result)
                        return 0
                    result["response"] = execute(session, command)
                except Exception as error:
                    result["error"] = str(error)
                publish(reply, result)
            time.sleep(.05)
        return 0
    finally:
        session.close()
        content.write_new(output/"closed.json", content.canonical(dict(pid=os.getpid(), bounded_session_closed=True)))


def invoke(args):
    session = private(args.session)
    require((session/"ready.json").is_file() and not (session/"closed.json").exists(), "No active captured editor content session")
    command = json.loads(args.command)
    token = uuid4().hex
    path = session/"requests"/(token+".json")
    # Publish a complete closed command file with exclusive destination naming.
    temporary = session/"requests"/(token+".tmp")
    content.write_new(temporary, content.canonical(command)); os.rename(temporary, path)
    reply = session/"responses"/path.name
    deadline = time.monotonic()+105
    while time.monotonic() < deadline:
        if reply.is_file():
            result = json.loads(content.read_bounded(reply, 8192))
            require(result["request_sha256"] == content.digest(content.canonical(command)), "Command/response identity mismatch")
            print(json.dumps(result))
            return 2 if result.get("error") else 0
        require(not (session/"closed.json").exists(), "Session closed before operation outcome; do not retry a mutation")
        time.sleep(.1)
    raise ValueError("Unknown operation outcome: inspect retained command/response; mutation was not retried")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest="mode", required=True)
    server = modes.add_parser("serve")
    server.add_argument("--config", required=True, type=Path)
    server.add_argument("--output", required=True, type=Path)
    server.add_argument("--seconds", type=int, default=600)
    client = modes.add_parser("invoke")
    client.add_argument("--session", required=True, type=Path)
    client.add_argument("--command", required=True)
    args = parser.parse_args()
    try:
        if args.mode == "serve":
            require(1 <= args.seconds <= 600, "Session duration must be bounded to 600 seconds")
            return serve(args)
        return invoke(args)
    except Exception as error:
        print(json.dumps(dict(error=str(error), mutation_retried=False)))
        return 2


if __name__ == "__main__":
    sys.exit(main())
