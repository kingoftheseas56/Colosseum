# Current publication and Cloud starting authority

Hemanth explicitly authorized Codex on 2026-10-20 to publish and verify
`codex/android-cloud-baseline-013-20261020`, then provide the exact Claude Code
Cloud setup and starting prompt. No integration is authorized.

This is the preserved candidate imported over upstream app commit
`fdd23f09e8b631f5c165e84d969895c99f520a28`, with its hardened donor source.
Codex will provide the actual remote-verified import SHA in the starting
prompt after publication. Never substitute master or infer the import SHA
from the upstream pin.

The packet's earlier PREPARED/NOT PUBLISHED statements and proposed permission
gates describe preparation history. This file and Hemanth's actual starting
prompt take precedence for the authorized Cloud session. Raw source and
hash/evidence records remain unchanged.

Keep the published baseline branch immutable during Claude implementation.
The supplied starting prompt proposes the dedicated worker branch
`claude/android-stabilization-013-20261020` for source checkpoints. When
Hemanth sends that prompt, follow its explicit worker-branch authority;
never infer permission to update master, another branch, a tag or a release.
No worker branch is created or pushed by this baseline publication.

Use `[skip ci]` for source checkpoint commits and preserve manual-only CI.
No Actions dispatch, PR, merge, integration, release or physical-device
operation is authorized for Claude Cloud. No laptop/SSH/BDC access is assumed.
Codex owns source/artifact/evidence review and later Nokia receiving.

Qualify actual Cloud source and large-APK return routes before implementation;
a local VM path or unverified Markdown download link is not durability proof.
If a platform denies a write, preserve the exact error and stop that action.
Do not reroute it, misreport a running worker, or erase the evidence.
