#pragma once

#include <QImage>
#include <QQuickImageProvider>
#include <QSize>

namespace Colosseum {

// Stateless EPUB cover decoder for image://vaultbookcover/<archive>/<entry>.
// The id uses the same base64url path/entry encoding as image://comiccover/;
// requests decode a bounded named ZIP entry and return a downscaled image.
class VaultBookCoverProvider final : public QQuickImageProvider
{
public:
    VaultBookCoverProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    QImage requestImage(const QString& id, QSize* size,
                        const QSize& requestedSize) override;
};

} // namespace Colosseum
