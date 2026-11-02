# Persistent milestone/checkpoint policy

- Commit and push every completed coherent mechanism after its focused checks.
- No more than15 minutes of productive edited work without a pushed checkpoint. If unfinished, use `WIP(android): <ID> <state>` and state failed/not-run checks.
- Push a checkpoint BEFORE any long-running setup/build/test. Refresh after it returns.
- Explicit-path staging only; no credentials, engine binaries, caches, logs, APKs or personal data in commits. Keep compact intentional progress files; full logs/artifacts stay in cloud artifact storage.
- Update docs/android/phase1-cloud/progress/STATUS.md and units.json every checkpoint. Include branch, unit IDs, test command+exit code, verification boundary, blockers and next action. Timestamp UTC where captured.
- Never infer pushed success from local commit; check push exit and remote ref. No force-push or protected/base branch mutation.
- If credit balance is not exposed, say UNKNOWN; never invent remaining credits or a spend estimate. Do not enable overages.
- Empty/unchanged idle work needs no heartbeat/empty commit. No new automation or worker loop.
