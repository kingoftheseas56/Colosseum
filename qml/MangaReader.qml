// The production reader (Task 13 cutover, 2026-07-25).
//
// This file used to BE the reader — ~2000 lines of Electron-recreation paging driven by
// ReaderEngine.js. It is now a thin delegation to the from-scratch Comic Reader in qml/comicreader/,
// which decides in C++ and paints in QML (ComicReaderCore + the image://comicreader/ provider).
//
// The filename stays because the callers stay: qml/MangaSeries.qml, qml/ComicSeries.qml,
// qml/ComicSeriesPage.qml and qml/BakeoffStripHost.qml all instantiate `MangaReader { ... }` and are
// UNCHANGED by this cutover — that was the design constraint of the whole rebuild, and
// tests/test_comicreader_migration.ps1 enforces both halves of it: this file must stay thin (no
// state, no behaviour), and the reader behind it must still honour the full Task 1 caller contract.
//
// Nothing belongs in here. A property or workaround added to this file is a second implementation
// of the reader; put it in comicreader/ComicReaderShell.qml (orchestration) or in the surface that
// owns the behaviour.
//
// Guided is ARCHIVED, not frozen-in-tree. It was still on disk when this reader was cut over, but
// Agent 2's 1d79fee removed the ONNX/read-along/guided arc from master on Hemanth's call — sources,
// harnesses and its gate. The whole thing lives on `archive/onnx-readalong-guided-2026-07-24`, one
// command from restoration. Nothing here reaches it: the reader's mode identity is Manga/Comic/Strip
// only, and tests/test_comicreader_surfaces.ps1 still greps this tree for any guided reference.

import QtQuick
import "comicreader"

ComicReaderShell { }
