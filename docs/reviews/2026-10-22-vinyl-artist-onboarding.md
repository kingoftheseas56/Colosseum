# Vinyl first-Home artist onboarding receiving record

Date: 2026-10-22. Scope: Task 1 of the personalized Home plan.
Branch: `codex/vinyl-production-flow`, starting at `ebc599e2d`.

## Received behavior

- An unanswered profile opens artist setup on its first meaningful Home visit. Leaving Home preserves the draft; other tabs keep their own query and navigation state.
- The existing top search opens artist search inside setup. It uses the real categorized YouTube Music catalogue through a separate `taste-artists` lane, with debounce, pagination, retry and stale-response/profile fencing. There is no second Home search field.
- Done accepts any number of selections, including zero. Skip records an empty answer. Both persist through the existing profile Preferences owner. Edit starts a draft; Cancel preserves the previous answer.
- Typed artist references retain provider identity, name and image. Legacy IDs still load without rewriting the file; whitespace/Unicode IDs survive subsequent edits. Validation and failed writes preserve existing state. Mapping statuses cannot falsely claim a canonical MusicBrainz identity.
- Answered Home shows a factual `Your artists` shelf. Its cards open the existing Artist destination. Local and saved artists can supply suggestions when public results are unavailable.
- Actions use the existing icon controls and accessible names. The shared prototype picker remains compatible with map and ID selections.

## Verification

The new preferences, native-service and UI tests first failed against missing behavior. Receiving review then added regressions for malformed/nonadvancing pagination, mapping-status consistency and legacy Unicode edits. The final Windows test-fixture failure was a read-only QFile left open across a QSettings save; closing the test handle resolved it.

Final build targets: `vinyl_taste_selections_test`, `tst_vinyl_feedback_snapshot`, `vinyl_native_session_bridge`, `vinyl_catalogue_source_test`, `colosseum_runtime_ready`.

**13/13 focused CTest suites passed**, in 55.57 seconds: taste selections, feedback snapshot, catalogue source, prototype Home, native Home, live world, personal library, onboarding UI, onboarding service, discovery, catalogue pages, player seek and playback queue. The prototype runner requires the installed Qt plugin path; its initial missing-offscreen-plugin timeout was an environment failure, followed by a passing rerun.

### Actual Main flow

`tests/vinyl-live/ArtistOnboardingFlow.qml` exercised production Main, native stores and catalogue transport using isolated test profiles. It invokes the actual QML actions; this is semantic runtime qualification, not a claim of OS pointer automation. QML suites separately exercise pointer/keyboard handlers.

| Phase | Result |
| --- | --- |
| Fresh Home | 40 real public artist suggestions returned; setup and Done/Skip visible. |
| Search/select | Real Nas (`UCPoQYATXIYvN5WB0c4f6jfQ`) and Ilaiyaraaja (`UCwzzSiogpjsYBMoyVcwDXwQ`) results selected. Draft survived a second query and Library/Home detour. |
| Done/routes | Both references saved; Your artists opened Artist and Back restored answered Home. |
| Restart/edit | Both artists restored; removing one in Edit then Cancel retained both saved choices. |
| Unavailable provider | Fresh profile with deliberately missing catalogue runtime could Skip; restart retained answered-empty state without another prompt. This is not whole-machine offline qualification. |
| Inspection | Answered Home left open. Both selected artist photos loaded after settling. Some earlier public image requests failed and displayed neutral placeholders. |

Exact tested application SHA-256: `8a6999a4faa181e942a19fbb8f1cf34de32b23eff90b73ee3b15ee839d05c7ac`.
QML tree manifest: `aaf49e263e716098de6ff53d6fd322a8397ec50eb6e4aa846c257065f4f4d132`.
External machine-local evidence: `D:/Colosseum-Vinyl-Onboarding-20261022/`, including `regression-final.log`, phase logs and captures. Final runtime profiles used the `done-03` and `skip-03` tags; personal profiles were not modified.

## Scope and remaining work

H4 is received. Selected artists are persisted inputs and direct destinations; recommendation candidates, enrichment, ranking and behavioral adaptation remain Tasks 2 onward. No personal YouTube feed access is claimed. Tankorent/remote seek qualification retains its existing separate gaps.

The pre-existing Main warning about `universeHallLayer` remains outside this slice. Bounded receiving review found no remaining concrete issue in the repaired preference compatibility path. This record establishes local build/tests/runtime evidence; it does not establish main-branch merge, release or device-wide acceptance.
