#pragma once

#include <QByteArray>
#include <QtGlobal>

#include <memory>

namespace TestProtocol {

class FtpFixture {
public:
    explicit FtpFixture(QByteArray payload);
    ~FtpFixture();
    [[nodiscard]] quint16 port() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

class NntpFixture {
public:
    explicit NntpFixture(QByteArray payload);
    ~NntpFixture();
    [[nodiscard]] quint16 port() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace TestProtocol
