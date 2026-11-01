#include "server1/http/ExternalCors.h"
#include "server1/http/HttpContract.h"
#include <QByteArray>
#include <QRegularExpression>
#include <QString>
#include <array>
#include <cstring>
namespace server1::http {
namespace {
bool allowed(const QString &origin) {
  if (!qgetenv("NO_CORS").isEmpty() || origin.isEmpty())
    return true;
  if (origin == QStringLiteral("https://stremio.github.io"))
    return true;
  static const std::array<QRegularExpression, 8> expressions = {
      QRegularExpression(".strem.io(:80)?$"),
      QRegularExpression(".stremio.net(:80)?$"),
      QRegularExpression(".stremio.com(:80)?$"),
      QRegularExpression("stremio-development.netlify.app(:80)?$"),
      QRegularExpression("stremio.github.io(:80)?$"),
      QRegularExpression("gstatic.com"),
      QRegularExpression("(127.0.0.1|localhost):11470$"),
      QRegularExpression("peario.xyz")};
  for (const auto &expression : expressions)
    if (expression.match(origin).hasMatch())
      return true;
  return false;
}
int cors(const void *req, void *response, server1_http_next_fn next,
         void *continuation, void *) {
  const char *origin = server1_http_request_header_value_at(req, "origin", 0);
  if (!allowed(QString::fromUtf8(origin)))
    return next(continuation);
  if (*origin) {
    server1_http_response_set_header(response, "Access-Control-Allow-Origin",
                                     "*");
    if (std::strcmp(server1_http_request_method(req), "OPTIONS") == 0) {
      const char *headers = server1_http_request_header_value_at(
          req, "access-control-request-headers", 0);
      server1_http_response_set_header(response, "Access-Control-Allow-Methods",
                                       "POST, GET, OPTIONS");
      server1_http_response_set_header(response, "Access-Control-Allow-Headers",
                                       *headers ? headers : "Range");
      server1_http_response_set_header(response, "Access-Control-Max-Age",
                                       "1728000");
      server1_http_response_set_status(response, 200);
      server1_http_response_set_body(response, "", 0);
      return 1;
    }
  }
  return next(continuation);
}
} // namespace
bool installExternalCors(void *router) {
  return server1_http_router_add_middleware(router, 0, "/", cors, nullptr,
                                            nullptr) != 0;
}
} // namespace server1::http
