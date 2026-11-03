#include "BookDownloader.h"
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkProxy>
#include <QUrlQuery>
#include <QStandardPaths>
#include <QDir>
#include <functional>

static const QString A(32, 'a'), B(32, 'b'), C(32, 'c');
class Fixture : public QTcpServer {
public:
    std::function<void(QTcpSocket*, const QUrl&)> handle;
    QList<QUrl> requests;
    Fixture() {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto s = nextPendingConnection();
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
                connect(s, &QTcpSocket::readyRead, this, [this,s] {
                    QByteArray data = s->property("request").toByteArray() + s->readAll();
                    s->setProperty("request", data);
                    if (!data.contains("\r\n\r\n") || s->property("handled").toBool()) return;
                    s->setProperty("handled", true);
                    QUrl url = QUrl::fromEncoded(data.split(' ').value(1));
                    requests.append(url);
                    handle(s, url);
                });
            }
        });
    }
    static void send(QTcpSocket* s, const QByteArray& body, int status=200, const QByteArray& type="application/epub+zip") {
        s->write("HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        s->disconnectFromHost();
    }
    static QByteArray links(const QString& md5, bool two=false) {
        QByteArray out = "<a href=\"get.php?md5=" + md5.toUtf8() + "&key=secret-one\">first</a>";
        if (two) out += "<a href=\"get.php?md5=" + md5.toUtf8() + "&key=secret-two\">second</a>";
        return out;
    }
};
class LocalNam : public QNetworkAccessManager {
public:
    quint16 port;
    QList<qint64> timeouts;
    explicit LocalNam(quint16 p) : port(p) { setProxy(QNetworkProxy::NoProxy); setTransferTimeout(250); }
protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& original, QIODevice* data) override {
        timeouts.append(original.transferTimeoutAsDuration().count());
        QNetworkRequest req(original);
        QUrl url(original.url()); url.setScheme("http"); url.setHost("127.0.0.1"); url.setPort(port);
        req.setUrl(url);
        return QNetworkAccessManager::createRequest(op, req, data);
    }
};
class NetworkTest : public QObject {
    Q_OBJECT
private:
    void clearOwnData() {
        const QString path = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
        const QString expectedSuffix = "/qttest/ColosseumNetworkTests/" + QCoreApplication::applicationName();
        QVERIFY(!path.isEmpty());
        QVERIFY(path.endsWith(expectedSuffix));
        QVERIFY(QCoreApplication::applicationName().startsWith("book-transport-"));
        if (QDir(path).exists()) QVERIFY(QDir(path).removeRecursively());
    }
private slots:
    void init() { clearOwnData(); }
    void cleanup() { clearOwnData(); }
    void queuedLinksAreResolvedOnlyWhenTransferSlotIsFree() {
        Fixture server; LocalNam nam(server.serverPort()); nam.setTransferTimeout(3000); BookDownloader books(&nam);
        QSignalSpy failed(&books,&BookDownloader::failed), done(&books,&BookDownloader::finished);
        QPointer<QTcpSocket> heldA;
        int bResolves=0; qint64 bKeyIssuedAt=-1; QElapsedTimer clock; clock.start();
        QStringList bStates;
        connect(&books,&BookDownloader::resolving,&books,[&](const QString& md5) {
            if(md5!=B) return;
            const QString state=books.statusOf(B).value("state").toString();
            bStates.append(state);
            bool found=false;
            for(const QVariant& value:books.activeBookJobs()) {
                const auto job=value.toMap();
                if(job.value("id").toString()==B) { found=true; QCOMPARE(job.value("state").toString(),state); }
            }
            QVERIFY(found);
        });
        server.handle=[&](QTcpSocket* socket,const QUrl& url) {
            const QString md5=QUrlQuery(url).queryItemValue("md5");
            if(url.path()=="/ads.php") {
                if(md5==B) { ++bResolves; bKeyIssuedAt=clock.elapsed(); }
                Fixture::send(socket,Fixture::links(md5));
            } else if(md5==A) heldA=socket;
            else if(clock.elapsed()-bKeyIssuedAt>300) Fixture::send(socket,"<html>expired</html>",200,"text/html");
            else Fixture::send(socket,"PKfresh");
        };
        books.downloadBook(A,"a.epub");
        QTRY_VERIFY_WITH_TIMEOUT(heldA,1000);
        books.downloadBook(B,"b.epub"); QTest::qWait(600);
        const QString waitingState=books.statusOf(B).value("state").toString();
        const int earlyResolves=bResolves;
        QVERIFY(heldA); Fixture::send(heldA,"PKfirst");
        QTRY_VERIFY_WITH_TIMEOUT(done.size()+failed.size()>=2,2000);
        QCOMPARE(earlyResolves,0); QCOMPARE(waitingState,"queued");
        QCOMPARE(bResolves,1); QCOMPARE(failed.size(),0); QCOMPARE(done.size(),2);
        QCOMPARE(done[0][0].toString(),A); QCOMPARE(done[1][0].toString(),B);
        QCOMPARE(bStates,QStringList({"queued","resolving","downloading"}));
    }
    void cancellingQueuedBookNeverResolvesIt() {
        Fixture server; LocalNam nam(server.serverPort()); nam.setTransferTimeout(3000); BookDownloader books(&nam);
        QSignalSpy done(&books,&BookDownloader::finished), removed(&books,&BookDownloader::removed), failed(&books,&BookDownloader::failed);
        QPointer<QTcpSocket> heldA; int bRequests=0;
        server.handle=[&](QTcpSocket* socket,const QUrl& url) {
            const QString md5=QUrlQuery(url).queryItemValue("md5");
            if(md5==B) ++bRequests;
            if(url.path()=="/ads.php") Fixture::send(socket,Fixture::links(md5));
            else if(md5==A) heldA=socket;
            else Fixture::send(socket,"PKother");
        };
        books.downloadBook(A,"a.epub"); QTRY_VERIFY_WITH_TIMEOUT(heldA,1000);
        books.downloadBook(B,"b.epub"); QTest::qWait(150); books.cancelDownload(B);
        QCOMPARE(books.statusOf(B).value("state").toString(),"none");
        QVERIFY(heldA); Fixture::send(heldA,"PKfirst");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,1000); QTest::qWait(150);
        QCOMPARE(bRequests,0); QCOMPARE(removed.size(),1); QCOMPARE(failed.size(),0);
    }
    void resolverFailureAdvancesQueue_data() {
        QTest::addColumn<int>("failureMode");
        QTest::newRow("http-error") << 0;
        QTest::newRow("no-links") << 1;
        QTest::newRow("cancelled") << 2;
    }
    void resolverFailureAdvancesQueue() {
        QFETCH(int,failureMode);
        Fixture server; LocalNam nam(server.serverPort()); nam.setTransferTimeout(3000); BookDownloader books(&nam);
        QSignalSpy done(&books,&BookDownloader::finished), failed(&books,&BookDownloader::failed), removed(&books,&BookDownloader::removed);
        QPointer<QTcpSocket> heldResolver; QStringList resolves;
        server.handle=[&](QTcpSocket* socket,const QUrl& url) {
            const QString md5=QUrlQuery(url).queryItemValue("md5");
            if(url.path()=="/ads.php") { resolves.append(md5); if(md5==A) heldResolver=socket; else Fixture::send(socket,Fixture::links(md5)); }
            else Fixture::send(socket,"PKnext");
        };
        // A terminal handler adds C while B is already queued. B must retain priority.
        auto addC=[&](const QString& md5) { if(md5==A) books.downloadBook(C,"c.epub"); };
        connect(&books,&BookDownloader::failed,&books,[&](const QString& md5,const QString&) { addC(md5); });
        connect(&books,&BookDownloader::removed,&books,addC);
        books.downloadBook(A,"a.epub"); QTRY_VERIFY_WITH_TIMEOUT(heldResolver,1000);
        books.downloadBook(B,"b.epub"); QTest::qWait(100);
        const QStringList requestsBeforeRelease=resolves;
        if(failureMode==0) Fixture::send(heldResolver,"error",503);
        else if(failureMode==1) Fixture::send(heldResolver,"no links",200,"text/html");
        else books.cancelDownload(A);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,2000);
        QCOMPARE(requestsBeforeRelease,QStringList({A}));
        QCOMPARE(resolves,QStringList({A,B,C}));
        QCOMPARE(done[0][0].toString(),B); QCOMPARE(done[1][0].toString(),C);
        QCOMPARE(failed.size(),failureMode==2 ? 0 : 1); QCOMPARE(removed.size(),failureMode==2 ? 1 : 0);
        QCOMPARE(books.statusOf(A).value("state").toString(),"none");
    }
    void successHandlerCannotJumpAheadOfQueuedBook() {
        Fixture server; LocalNam nam(server.serverPort()); nam.setTransferTimeout(3000); BookDownloader books(&nam);
        QSignalSpy done(&books,&BookDownloader::finished); QPointer<QTcpSocket> heldA; QStringList resolves;
        server.handle=[&](QTcpSocket* socket,const QUrl& url) {
            const QString md5=QUrlQuery(url).queryItemValue("md5");
            if(url.path()=="/ads.php") { resolves.append(md5); Fixture::send(socket,Fixture::links(md5)); }
            else if(md5==A) heldA=socket;
            else Fixture::send(socket,"PKnext");
        };
        connect(&books,&BookDownloader::finished,&books,[&](const QString& md5,const QString&) { if(md5==A) books.downloadBook(C,"c.epub"); });
        books.downloadBook(A,"a.epub"); QTRY_VERIFY_WITH_TIMEOUT(heldA,1000);
        books.downloadBook(B,"b.epub"); QTest::qWait(100); Fixture::send(heldA,"PKfirst");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,2000);
        QCOMPARE(resolves,QStringList({A,B,C}));
        QCOMPARE(done[0][0].toString(),A); QCOMPARE(done[1][0].toString(),B); QCOMPARE(done[2][0].toString(),C);
    }
    void networkManagerMayBeDestroyedFirst() {
        Fixture server;
        auto* nam = new LocalNam(server.serverPort());
        auto* books = new BookDownloader(nam);
        server.handle=[](QTcpSocket*,const QUrl&) {};
        books->downloadBook(A,"a.epub");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(),1,1000);
        delete nam;
        // Assert before invoking Books' destructor: a stale raw reply would crash there.
        // On the red baseline QFAIL intentionally leaves this isolated Books object alive.
        QVERIFY2(books->statusOf(A).value("state").toString()=="none", "Destroyed NAM left a dangling resolver entry");
        delete books;
    }
    void downloaderMayBeDestroyedFirst() {
        Fixture server; LocalNam nam(server.serverPort());
        auto* books = new BookDownloader(&nam);
        server.handle=[](QTcpSocket*,const QUrl&) {};
        books->downloadBook(A,"a.epub");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(),1,1000);
        QSignalSpy failed(books,&BookDownloader::failed), done(books,&BookDownloader::finished);
        delete books;
        QTest::qWait(600);
        QCOMPARE(failed.size(),0); QCOMPARE(done.size(),0);
    }
    void normalSuccessUsesDefaultDeadline() {
        Fixture server; LocalNam nam(server.serverPort()); nam.setTransferTimeout(0); BookDownloader books(&nam);
        QSignalSpy failed(&books,&BookDownloader::failed), done(&books,&BookDownloader::finished);
        server.handle=[](QTcpSocket* s,const QUrl& u) {
            if(u.path()=="/ads.php") Fixture::send(s,Fixture::links(A));
            else Fixture::send(s,"PKnormal");
        };
        books.downloadBook(A,"a.epub");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,1000);
        QCOMPARE(nam.timeouts,QList<qint64>({30000,30000}));
        QCOMPARE(books.statusOf(A).value("state").toString(),"done");
        QFile file(books.localBook(A)); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),"PKnormal");
        QCOMPARE(failed.size(),0);
    }
    void resolverAbsoluteDeadline() {
        Fixture server; LocalNam nam(server.serverPort()); BookDownloader books(&nam);
        QSignalSpy failed(&books, &BookDownloader::failed), done(&books, &BookDownloader::finished);
        server.handle = [&](QTcpSocket* s, const QUrl& url) {
            QString md5=QUrlQuery(url).queryItemValue("md5");
            if (md5 == A) {
                s->write("HTTP/1.1 200 OK\r\nContent-Length: 99999\r\n\r\n");
                auto timer = new QTimer(s); timer->setInterval(40);
                connect(timer,&QTimer::timeout,s,[s] { s->write(" "); }); timer->start();
            } else if(url.path()=="/ads.php") Fixture::send(s, Fixture::links(md5));
            else Fixture::send(s,"PKfixture");
        };
        books.downloadBook(A,"a.epub"); books.downloadBook(B,"b.epub");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,2000);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,2000);
        QCOMPARE(failed[0][0].toString(),A);
        QVERIFY(failed[0][1].toString().contains("timed out",Qt::CaseInsensitive));
        QCOMPARE(books.statusOf(A).value("state").toString(),"none");
    }
    void retryExhaustionAndFailover() {
        Fixture server; LocalNam nam(server.serverPort()); BookDownloader books(&nam);
        QSignalSpy failed(&books,&BookDownloader::failed), done(&books,&BookDownloader::finished);
        int first=0,second=0;
        QElapsedTimer elapsed; elapsed.start(); QList<qint64> firstTimes;
        server.handle=[&](QTcpSocket* s,const QUrl& u) {
            if(u.path()=="/ads.php") Fixture::send(s,Fixture::links(A,true));
            else if(QUrlQuery(u).queryItemValue("key")=="secret-one") { ++first; firstTimes.append(elapsed.elapsed()); Fixture::send(s,"error",503); }
            else { ++second; Fixture::send(s,"PKfixture"); }
        };
        books.downloadBook(A,"a.epub");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,11000);
        QCOMPARE(first,3); QCOMPARE(second,1); QCOMPARE(failed.size(),0);
        QVERIFY(firstTimes[1]-firstTimes[0] >= 1800);
        QVERIFY(firstTimes[2]-firstTimes[1] >= 3600);
        QFile file(books.localBook(A)); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),"PKfixture");
    }
    void downloadTimeoutAdvancesQueue() {
        Fixture server; LocalNam nam(server.serverPort()); BookDownloader books(&nam);
        QSignalSpy failed(&books,&BookDownloader::failed), done(&books,&BookDownloader::finished);
        int attempts=0;
        server.handle=[&](QTcpSocket* s,const QUrl& u) {
            QString md5=QUrlQuery(u).queryItemValue("md5");
            if(u.path()=="/ads.php") Fixture::send(s,Fixture::links(md5));
            else if(md5==A) { ++attempts; } // accepted request, never sends a response
            else Fixture::send(s,"PKqueued");
        };
        books.downloadBook(A,"a.epub");
        QTRY_COMPARE_WITH_TIMEOUT(attempts,1,1000);
        books.downloadBook(B,"b.epub");
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,12000);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,2000);
        QCOMPARE(attempts,3); QCOMPARE(done[0][0].toString(),B);
        QVERIFY(failed[0][1].toString().contains("timed out",Qt::CaseInsensitive));
    }
    void cancelResolverHasNoLateTerminalEvent() {
        Fixture server; LocalNam nam(server.serverPort()); BookDownloader books(&nam);
        QSignalSpy failed(&books,&BookDownloader::failed), done(&books,&BookDownloader::finished), removed(&books,&BookDownloader::removed);
        server.handle=[](QTcpSocket*,const QUrl&) {};
        books.downloadBook(A,"a.epub");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(),1,1000);
        books.cancelDownload(A);
        QTest::qWait(600);
        QCOMPARE(removed.size(),1); QCOMPARE(failed.size(),0); QCOMPARE(done.size(),0);
        QCOMPARE(books.statusOf(A).value("state").toString(),"none");
    }
    void failureHandlerCanRestartSameBook() {
        Fixture server; LocalNam nam(server.serverPort()); BookDownloader books(&nam);
        QSignalSpy failed(&books,&BookDownloader::failed), done(&books,&BookDownloader::finished);
        int resolves=0;
        server.handle=[&](QTcpSocket* s,const QUrl& u) {
            if(u.path()=="/ads.php") { ++resolves; Fixture::send(s,Fixture::links(A)); }
            else if(resolves==1) Fixture::send(s,"<html>expired</html>",200,"text/html");
            else Fixture::send(s,"PKretry");
        };
        connect(&books,&BookDownloader::failed,&books,[&](const QString& md5,const QString&) { books.downloadBook(md5,"restart.epub"); });
        books.downloadBook(A,"a.epub");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,2000);
        QCOMPARE(failed.size(),1); QCOMPARE(resolves,2);
        QVERIFY(failed[0][1].toString().contains("HTML"));
    }
    void cancelRestartDuringRetry() {
        Fixture server; LocalNam nam(server.serverPort()); nam.setTransferTimeout(8000); BookDownloader books(&nam);
        QSignalSpy failed(&books,&BookDownloader::failed), done(&books,&BookDownloader::finished), removed(&books,&BookDownloader::removed);
        int resolves=0,gets=0; QPointer<QTcpSocket> restarted;
        server.handle=[&](QTcpSocket* s,const QUrl& u) {
            if(u.path()=="/ads.php") { ++resolves; Fixture::send(s,Fixture::links(A)); }
            else { ++gets; if(resolves==1) Fixture::send(s,"error",503); else restarted=s; }
        };
        books.downloadBook(A,"a.epub");
        QTRY_COMPARE_WITH_TIMEOUT(gets,1,1000); QTest::qWait(200);
        books.cancelDownload(A); books.downloadBook(A,"restart.epub");
        QTRY_COMPARE_WITH_TIMEOUT(gets,2,1000); QTest::qWait(2300);
        QCOMPARE(gets,2); QCOMPARE(removed.size(),1); QCOMPARE(failed.size(),0);
        QVERIFY(restarted); Fixture::send(restarted,"PKrestart");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,1000);
        QTest::qWait(300); QCOMPARE(failed.size(),0); QCOMPARE(done.size(),1);
    }
};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    app.setOrganizationName("ColosseumNetworkTests"); app.setApplicationName("book-transport-" + QString::number(QCoreApplication::applicationPid()));
    QStandardPaths::setTestModeEnabled(true);
    NetworkTest test; return QTest::qExec(&test,argc,argv);
}
#include "book_downloader_network.moc"
