#include "server1/http/HttpContract.h"
#include "server1/network/ProxyService.h"
#include <QNetworkAccessManager>
namespace server1::network {
int startProxy(QNetworkAccessManager *, const void *, void *);
int registerProxyRoutes(void *router) {
  auto *manager = new QNetworkAccessManager;
  int ok = server1_http_router_add_handler(
      router, 0, 1, "ALL", "/proxy",
      [](const void *req, void *res, server1_http_next_fn next, void *nc,
         void *ctx) {
        return startProxy(static_cast<QNetworkAccessManager *>(ctx), req, res)
                   ? 1
                   : next(nc);
      },
      manager, [](void *p) { delete static_cast<QNetworkAccessManager *>(p); });
  if (!ok)
    delete manager;
  return ok;
}
} // namespace server1::network
