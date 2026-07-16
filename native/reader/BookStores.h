// BookStores.h
//
// Shared JSON store helpers, lifted out of BookBridge.cpp (the OLD reader's
// persistence) so the NEW reader (reader2) can read/write the EXACT SAME files
// under <appdata>/book_reader/ — progress.json, settings.json, bookmarks.json,
// annotations.json, display_names.json — with zero migration. Both readers call
// into this namespace; neither owns its own copy of the store logic.
//
// The directory resolves via QStandardPaths::writableLocation(AppDataLocation)
// (+ "/book_reader"), same as before. Under QStandardPaths::setTestModeEnabled(true)
// (set by test harnesses) that location is automatically redirected to a sandbox,
// so tests never touch a real user's stores.

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace BookStores {

// ── raw whole-file JSON object store ──
QJsonObject readStore(const QString& fileName);
void        writeStore(const QString& fileName, const QJsonObject& all);

// ── keyed single-object pattern (progress.json: bookId -> object) ──
QJsonObject get(const QString& fileName, const QString& bookId);
void        save(const QString& fileName, const QString& bookId, const QJsonObject& data);

// ── shared {bookId: [items]} list pattern (bookmarks.json / annotations.json) ──
QJsonArray  listGet(const QString& fileName, const QString& bookId);
QJsonObject listSave(const QString& fileName, const QString& bookId, QJsonObject item);
QJsonObject listDelete(const QString& fileName, const QString& bookId, const QString& itemId);
void        listClear(const QString& fileName, const QString& bookId);

} // namespace BookStores
