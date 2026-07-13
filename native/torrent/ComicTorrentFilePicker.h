#pragma once

#include "BookTorrentFilePicker.h" // ManifestFile, PickedFile

class ComicTorrentFilePicker
{
public:
    static PickedFile pick(const QString& title, const QList<ManifestFile>& files);
    static bool isComicArchive(const QString& name);
    static int formatRank(const QString& ext);
    static QString extOf(const QString& name);
};
