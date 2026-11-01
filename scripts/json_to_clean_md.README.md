# Clean transcripts and bounded fact-review batches

Run with Python 3.10+, no third-party packages. Raw inputs remain untouched.

From the repository root:

    python scripts/json_to_clean_md.py "$env:USERPROFILE\.claude\projects" -o "D:\Transcript-Facts\new-export\Claude"
    python scripts/json_to_clean_md.py "$env:USERPROFILE\.codex\sessions" -o "D:\Transcript-Facts\new-export\Codex"

Use a fresh output directory. Directory input is scoped to this repository.
Brotherhood excludes nested Colosseum. Repeat --include-workspace PATH for
worktrees; --exclude-workspace PATH removes other projects. --all-workspaces
is explicit and still excludes restricted Preflight paths. --stable-seconds 120
accounts for active/recent files rather than exporting a moving source.

Claude top-level cwd/sessionId and Codex metadata are recognized. Exports retain
message timestamps and JSONL line / JSON pointer anchors. Context-compaction
summaries are labeled as generated evidence. System/developer instructions,
tool records, internal reasoning, attachments and injected memory are omitted.
Credential patterns are redacted on a best-effort basis; outputs remain private.
Repeated genuine dialogue is retained; adjacent Codex duplicate representations
are combined with both raw anchors.

INDEX.md, MANIFEST.csv and REPORT.json account for every discovered candidate:
exported, skipped with reason, or failed. The manifest includes raw and Markdown
SHA-256 values, source size, platform, recorded dates and redaction counts. Inputs
are checked for changes during conversion; hashes attest bytes, not claim truth.
A skipped active file remains outstanding. Dates absent from source stay unknown.

Build bounded Dots packets after export:

    python scripts/build_fact_batches.py PATH_TO_MANIFEST.csv -o "D:\Transcript-Facts\new-export\packets"

Multiple manifests can be supplied. Each private ZIP includes a prompt, checked
fragment manifest and up to 10 fragments / 180,000 transcript characters. Oversize
sessions split with exact original clean-line/character offsets. Raw archives and
memory stores are not bundled. BATCHES.json records packet hashes and coverage.
Ordering is by recorded session start; event chronology requires later reconciliation.

Dots returns cited FACTS.json, TIMELINE.md, CONTRADICTIONS.json and COVERAGE.json.
Requests, plans, claims, cited evidence and independently verified facts stay separate.
After extraction, reconcile across batches and verify decisive claims against the
raw evidence/live state before updating canonical facts. Preparation sends nothing.
Do not commit private transcripts or packets into a repository.
