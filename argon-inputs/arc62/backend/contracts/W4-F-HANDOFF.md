# W4-F Catalogue + Timed-Text Identity Handoff

AGENT: Agent-6
TASK: T-6 / W4-F
STATE: DONE
TIME: 2026-10-06T20:05:09+05:30

COMPLETED:
- BG1 direct canonical lookups now distinguish Found, Redirected, and typed failure; merged MBIDs no longer collapse into NotFound.
- Added deterministic ordered batch canonical lookup with explicitly non-authoritative LastKnownDisplaySnapshot { primaryText, artistCredit }.
- Added representativeRelease(ReleaseGroupMbid) using catalogue canonical-release mapping; missing mapping fails closed instead of guessing an edition.
- BC1 TimedTextDocument now carries deterministic revision-scoped TimedTextDocumentId derived from Recording + provider/item + content/timing evidence.
- BC1 TimedTextRequestKey is RecordingMbid + BA1 QueueOccurrenceId, so same-occurrence source restarts preserve still-valid lyric ownership.
- Consumed W4-A recording-time law: recordingMs = trackRelativeMs + recordingTimeOffsetMs; unknown offset stays unknown.
- Added source-aware lyric highlight/seek helpers; unknown recording-time offset returns no synchronized result.
- BC2 translations carry source TimedTextDocumentId and projection/request ownership fails with SourceDocumentMismatch on document swap.
- BC3 cue-linked referents carry source TimedTextDocumentId, validate cue membership, and resolve only against the exact source revision.
- Provider/document swap fixture reuses cue IDs deliberately and proves neither translation nor annotation silently migrates.

VERIFIED:
- MSVC 19.44 + Qt 6.11.1: BG1 configure/build + vinyl_bg1_catalogue_harness 1/1 passed.
- MSVC 19.44 + Qt 6.11.1: BC1 configure/build + vinyl_bc1_timed_text_harness 1/1 passed.
- MSVC 19.44 + Qt 6.11.1: BC2 configure/build + vinyl_bc2_translation_harness 1/1 passed.
- MSVC 19.44 + Qt 6.11.1: BC3 configure/build + vinyl_bc3_annotation_meaning_harness 1/1 passed.
- After T-4 published its frozen BA0/BA1 interfaces, BC1/BC2/BC3 were rebuilt against that final shared surface and all three harnesses again passed 1/1.
- Final direct execution of all four harness binaries exited 0.
- Static audit finds no occurrenceGeneration wording left in BC1/BC2 contracts and confirms redirect/display-snapshot/document-id/recording-offset surfaces.

CHANGED: BG1-CATALOGUE-CLIENT-CONTRACT.md; bg1-catalogue-client-prototype/; BC1-TIMED-TEXT-CONTRACT.md; bc1-timed-text-prototype/; BC2-TRANSLATION-CONTRACT.md; bc2-translation-prototype/; BC3-ANNOTATION-MEANING-CONTRACT.md; bc3-annotation-prototype/.
UNRESOLVED: None inside T-6. Production Colosseum adoption remains outside this prototype/reference slice.
NEXT: Lead may unblock W4-G when T-4 and T-5 are compatible; W4-H later consumes these interfaces in the composed Play journey.
IMPORTANT EVIDENCE: W4-A public interface at WAVE4-AGENT-ROOM.md lines 566-591; BG1/BC1/BC2/BC3 native harnesses above; provider/document-swap regression tests in BC2 and BC3.
