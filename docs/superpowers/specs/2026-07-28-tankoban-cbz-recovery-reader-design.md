# Tankoban CBZ Recovery and Reader Design

**Owner:** [Agent 1 (Codex), comics]

## Decision

Tankoban volume storage is CBZ-only. This follows Tankoban 2's actual
`WeebCentralVolumePacker` lifecycle: chapter images are temporary staging
material, then zipped, validated, atomically finalized as `.cbz`, and read
directly from the archive.

This decision applies to both Tankoban sources:

- Nyaa archives are retained as CBZ when already ZIP-based. Non-ZIP comic
  archives may be converted through temporary extraction, but the durable
  library artifact is always CBZ.
- WeebCentral images exist only in a temporary staging directory. A completed
  volume is written atomically as CBZ; no durable loose-page copy remains.

The generic Comic Reader may continue accepting local-file page descriptors for
western-comic and preview callers outside Tankoban volume storage. Tankoban's
`MangaVolumeIndex` emits archive-backed descriptors exclusively after migration.

## Durable layout

```text
<AppData>/manga-volumes/
  volume-index.json
  archives/<series-segment>/vol-<number>-<id-hash>.cbz
  archives/<series-segment>/vol-<number>-<id-hash>.cbz.json
```

The `.cbz.json` sidecar is the per-volume recovery authority. It stores:

- volume, series, source, and release provenance;
- naturally ordered CBZ entry names;
- chapter-group ordinal per page;
- payload byte count and added time.

`volume-index.json` is the global lookup ledger. It is derived/recoverable
state, not the sole authority for a volume's page list.

## Archive primitive

Add one small miniz-backed CBZ primitive used by ingestion, migration, indexing,
and decode:

```cpp
struct CbzPageEntry {
    QString name;
    quint64 uncompressedBytes = 0;
};

class CbzArchive {
public:
    static QVector<CbzPageEntry> imageEntries(const QString& archivePath,
                                              QString* error = nullptr);
    static QByteArray readEntry(const QString& archivePath,
                                const QString& entryName,
                                QString* error = nullptr);
    static bool writeImagesAtomic(const QString& archivePath,
                                  const QString& sourceDir,
                                  const QStringList& orderedRelativeFiles,
                                  QString* error = nullptr);
};
```

Entries are filtered to supported image suffixes, macOS resource forks and
hidden files are ignored, and the result is naturally sorted. Writes go to a
same-directory `.part`, finalize the ZIP, validate it by reopening it, then
atomically rename it to `.cbz`. Images use store/no-compression because their
formats are already compressed.

## Index repair and legacy migration

`MangaVolumeIndex::heal()` follows repair-before-prune:

1. If a row points to an intact CBZ and valid sidecar, reconcile the global row
   from the sidecar and archive entry list, then atomically save the ledger.
2. If a row points to the old loose directory, read `index.json`, verify every
   manifest page exists, and repair incorrect ledger filenames from that
   manifest.
3. Convert the verified loose pages into the canonical CBZ and write its
   sidecar.
4. Save the repaired CBZ ledger row atomically.
5. Reopen the CBZ and confirm the expected page count/names.
6. Only after steps 1–5 succeed, remove the old loose directory.
7. If neither CBZ nor legacy manifest is recoverable, remove the false-ready
   ledger row but leave payload files untouched for manual recovery.

This ordering makes Volume 1's correct per-volume manifest repair the corrupt
global extensions without risking its 216 files. Volume 70 follows the same
migration path.

## Direct reader source

Extend `PageMeta` with an explicit source kind:

```cpp
enum class PageSourceKind { LocalFile, CbzEntry };

PageSourceKind sourceKind = PageSourceKind::LocalFile;
QString localPath;
QString archivePath;
QString archiveEntry;
```

`MangaVolumeIndex::localPages()` returns maps containing `archive` and `entry`
for Tankoban volumes. `ComicReaderCore::parsePages()` recognizes those maps.
`ComicReaderDecode` reads a CBZ entry to memory through `CbzArchive::readEntry`
and then uses the existing `QBuffer`/`QImageReader` decode pipeline. The cache,
generation guard, pairing, strip geometry, and provider remain unchanged.

Decode diagnostics include generation, logical page, archive path, entry name,
and the exact archive-open/lookup/extract failure. Successful opens remain
quiet. This is the instrumentation needed to identify an intermittent Volume 70
failure without guessing.

## Cursor ownership

Replace the conditional `cursorHideArea` with a persistent top-level hover
MouseArea that remains enabled while the reader is open:

```qml
hoverEnabled: true
enabled: !reader.modalOpen
cursorShape: reader.chromeVisible ? Qt.ArrowCursor : Qt.BlankCursor
onPositionChanged: reader.wakeChrome()
```

The item must not disable itself when movement makes the HUD visible. Modal and
HUD controls remain above it and keep their pointing-hand cursors. This mirrors
the working Player 1/Player 2 ownership pattern.

## Verification

- A corrupt global row with a correct legacy manifest repairs and migrates
  without deleting source pages.
- A failed CBZ write leaves the legacy directory and prior ledger untouched.
- A valid CBZ sidecar repairs a corrupted global ledger.
- WeebCentral packing and Nyaa ingestion end with a CBZ plus sidecar and no
  durable loose pages.
- CBZ entries decode through the real reader and preserve natural order/groups.
- Cursor harness proves the cursor owner stays enabled and switches to Arrow
  when chrome becomes visible.
- Windows smoke repeats idle-hide then pointer-wake and verifies the OS cursor
  handle returns to the system arrow.
- Live Volume 1 and Volume 70 migrate with the app closed, then both open in the
  reader.

