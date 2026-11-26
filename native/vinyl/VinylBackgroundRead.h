#pragma once

#include <QFutureWatcher>
#include <QPromise>
#include <QThreadPool>
#include <functional>
#include <type_traits>
#include <utility>

// Work owns only value snapshots. QObject access and publication belong in
// done, which is discarded when its owner is destroyed.
namespace vinyl {
template<class Work, class Done>
void backgroundRead(QObject *owner, Work work, Done done) {
    using Result = std::invoke_result_t<Work>;
    QPromise<Result> promise;
    auto *watcher = new QFutureWatcher<Result>(owner);
    QObject::connect(watcher, &QFutureWatcher<Result>::finished, owner,
        [watcher, done=std::move(done)]() mutable {
            auto result = watcher->result();
            watcher->deleteLater();
            done(std::move(result));
        });
    watcher->setFuture(promise.future());
    QThreadPool::globalInstance()->start([promise=std::move(promise), work=std::move(work)]() mutable {
        promise.start();
        promise.addResult(work());
        promise.finish();
    });
}
}
