#!/usr/bin/env python3
"""Run real DB tests in a new, private PostgreSQL cluster (never an external URL).

Requires Python 3, Go (module toolchain), PostgreSQL server/client binaries on
PATH, and an unprivileged Unix user. Run from any directory. No Docker required.
Logs survive cleanup in the printed evidence directory. Exit nonzero on skips,
no tests, provisioning failures, or test failures. Optional: --run TEST_REGEX.
"""
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import sys
import tempfile


def main():
    # Reject before even allocating a directory or invoking PostgreSQL. Never
    # reinterpret a supplied URL, including a misleading production _test alias.
    valid_args = len(sys.argv) == 1 or (len(sys.argv) == 3 and sys.argv[1] == "--run" and bool(sys.argv[2]))
    if not valid_args or any(k in os.environ for k in (
        "TEST_DATABASE_URL", "DATABASE_URL", "MIGRATION_DATABASE_URL", "PGSERVICE",
        "PGSERVICEFILE", "PGHOST", "PGDATABASE", "PGPORT", "PGUSER", "PGPASSWORD",
    )):
        sys.exit("refusing external database configuration/arguments; unset database variables")
    if os.geteuid() == 0:
        sys.exit("run as an unprivileged user")
    for tool in ("initdb", "pg_ctl", "createdb", "psql", "postgres", "go"):
        if not shutil.which(tool):
            sys.exit(f"missing required tool: {tool}")
    root = Path(tempfile.mkdtemp(prefix="account-db-", dir="/tmp"))
    print(f"evidence: {root}", flush=True)
    data = root / "data"
    password = secrets.token_hex(24)
    password_file = root / "password"
    password_file.write_text(password)
    password_file.chmod(0o600)
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = str(reservation.getsockname()[1])
    env = {k: v for k, v in os.environ.items() if not k.startswith("PG")}
    name = "colosseum_disposable_test"
    env.update(PGHOST="127.0.0.1", PGPORT=port, PGUSER="fixture", PGDATABASE=name, PGPASSWORD=password)
    started = False

    def run(args, log):
        with (root / log).open("a") as out:
            subprocess.run(args, cwd=Path(__file__).resolve().parents[1], env=env,
                           stdout=out, stderr=subprocess.STDOUT, check=True)

    try:
        run(["initdb", "-D", str(data), "-U", "fixture", "--auth-local=reject", "--auth-host=scram-sha-256", "--pwfile", str(password_file), "--no-locale", "--encoding=UTF8"], "provision.log")
        run(["pg_ctl", "-D", str(data), "-l", str(root / "postgres.log"),
             "-o", f"-c listen_addresses=127.0.0.1 -c port={port} -c unix_socket_directories=''", "-w", "start"], "provision.log")
        started = True
        run(["createdb", name], "provision.log")
        # Check server identity before any Go helper can drop a schema.
        sql = "SELECT current_database(), current_user, current_setting('data_directory'), current_setting('listen_addresses')"
        identity = subprocess.check_output(["psql", "-X", "-A", "-t", "-F", "|", "-c", sql], env=env, text=True).strip()
        if identity != f"{name}|fixture|{data}|127.0.0.1" or not name.endswith("_test"):
            raise RuntimeError("disposable cluster identity mismatch")
        (root / "identity.log").write_text(identity + "\n")
        for command in (["go", "version"], ["postgres", "--version"]):
            run(command, "identity.log")
        env["TEST_DATABASE_URL"] = f"postgres://fixture:{password}@127.0.0.1:{port}/{name}?sslmode=disable"
        # All backend tests in these packages, serial because public is reset.
        # The Windows Qt executable probe is outside this backend fixture gate;
        # explicitly exclude it rather than accepting a skipped test as green.
        # -json includes -v events and the exact selector is retained.
        command = ["go", "test", "-json", "-v", "-count=1", "-p", "1", "-race",
                   "-run", sys.argv[2] if len(sys.argv) == 3 else ".",
                   "-skip", "^TestDesktopRefreshResponseLoss$",
                   "./internal/testsupport/testdb", "./internal/account", "./internal/httpserver"]
        (root / "command.json").write_text(json.dumps(command) + "\n")
        with (root / "tests.jsonl").open("w") as stdout, (root / "test-stderr.log").open("w") as stderr:
            result = subprocess.run(command, cwd=Path(__file__).resolve().parents[1], env=env,
                                    stdout=stdout, stderr=stderr)
        events = [json.loads(line) for line in (root / "tests.jsonl").read_text().splitlines()]
        counts = {action: sum(e.get("Action") == action and "Test" in e for e in events)
                  for action in ("run", "pass", "skip", "fail")}
        top = {action: sum(e.get("Action") == action and "Test" in e and "/" not in e["Test"] for e in events)
               for action in counts}
        summary = {"events_including_subtests": counts, "top_level_tests": top, "go_exit": result.returncode, "race": True}
        (root / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
        print(json.dumps(summary), flush=True)
        return 1 if result.returncode or counts["skip"] or counts["fail"] or not counts["run"] or counts["run"] != counts["pass"] else 0
    finally:
        if started or (data / "postmaster.pid").exists():
            run(["pg_ctl", "-D", str(data), "-m", "immediate", "-w", "stop"], "provision.log")
        # Retain evidence, but never the disposable database contents.
        if data.exists():
            shutil.rmtree(data)
        password_file.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
