#pragma once

#include <QByteArray>
#include <QByteArrayView>

class QIODevice;

namespace Colosseum::Update {

bool verifyEd25519Raw(QByteArrayView message, QByteArrayView signature,
                      QByteArrayView publicKey, QString* error);
QByteArray sha256(QIODevice* device, QString* error);
QByteArrayView embeddedUpdatePublicKey();

} // namespace Colosseum::Update
