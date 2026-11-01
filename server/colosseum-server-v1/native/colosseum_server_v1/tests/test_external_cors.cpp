#include "server1/http/ExternalCors.h"
#include "server1/http/HttpContract.h"
#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstring>
#include <iostream>
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  qunsetenv("NO_CORS");
  auto check = [](bool v, const char *why) {
    if (!v) {
      std::cerr << why << '\n';
      std::exit(1);
    }
  };
  void *router = server1_http_router_create();
  check(server1::http::installExternalCors(router), "install");
  server1_http_router_add_handler(
      router, 0, 0, "ALL", "/handled",
      [](const void *, void *response, server1_http_next_fn, void *, void *) {
        server1_http_response_set_status(response, 201);
        server1_http_response_set_body(response, "handled", 7);
        return 1;
      },
      nullptr, nullptr);
  auto request = [&](QByteArray method, QByteArray origin,
                     QByteArray headers = QByteArray{}) {
    QByteArray raw = method + " /handled HTTP/1.1\r\nHost: localhost\r\n";
    if (!origin.isNull())
      raw += "Origin: " + origin + "\r\n";
    if (!headers.isNull())
      raw += "Access-Control-Request-Headers: " + headers + "\r\n";
    raw += "\r\n";
    auto *parser = server1_http_parser_create(1024);
    check(server1_http_parser_feed(parser, raw.constData(), raw.size()) == 1,
          "parse");
    auto *r = server1_http_router_dispatch_parsed(router, parser);
    server1_http_parser_destroy(parser);
    return r;
  };
  for (auto origin :
       {QByteArray("https://app.strem.io"),
        QByteArray("https://appXstremioYcom"),
        QByteArray("https://stremio.github.io"),
        QByteArray("https://evil.test/gtest.gstatic.com/elsewhere"),
        QByteArray("http://localhost:11470"),
        QByteArray("https://prefixpearioXxyzsuffix")}) {
    auto *r = request("GET", origin);
    if (server1_http_response_status(r) != 201 ||
        !*server1_http_response_header(r, "access-control-allow-origin"))
      std::cerr << origin.constData()
                << " status=" << server1_http_response_status(r) << "\n";
    check(server1_http_response_status(r) == 201 &&
              !std::strcmp(server1_http_response_header(
                               r, "access-control-allow-origin"),
                           "*"),
          "allowed origin/source regex");
    server1_http_response_destroy(r);
  }
  for (auto origin :
       {QByteArray("https://evil.test"), QByteArray("http://localhost:11471"),
        QByteArray("https://app.strem.io:443")}) {
    auto *r = request("GET", origin);
    check(server1_http_response_status(r) == 201 &&
              !*server1_http_response_header(r, "access-control-allow-origin"),
          "disallowed origin");
    server1_http_response_destroy(r);
    r = request("OPTIONS", origin);
    check(server1_http_response_status(r) == 201 &&
              !*server1_http_response_header(r, "access-control-allow-origin"),
          "disallowed preflight falls through");
    server1_http_response_destroy(r);
  }
  for (auto method : {"GET", "POST", "OPTIONS"}) {
    auto *r = request(method, QByteArray{});
    check(server1_http_response_status(r) == 201 &&
              !*server1_http_response_header(r, "access-control-allow-origin"),
          "no Origin falls through");
    server1_http_response_destroy(r);
  }
  auto *r = request("POST", "http://localhost:11470");
  check(server1_http_response_status(r) == 201 &&
            !std::strcmp(
                server1_http_response_header(r, "access-control-allow-origin"),
                "*"),
        "POST annotated not intercepted");
  server1_http_response_destroy(r);
  for (auto headers :
       {QByteArray{}, QByteArray(""), QByteArray("Range, X-Test")}) {
    r = request("OPTIONS", "http://localhost:11470", headers);
    check(server1_http_response_status(r) == 200 &&
              server1_http_response_body_size(r) == 0,
          "preflight ends empty");
    check(!std::strcmp(
              server1_http_response_header(r, "access-control-allow-methods"),
              "POST, GET, OPTIONS"),
          "methods");
    check(
        !std::strcmp(server1_http_response_header(r, "access-control-max-age"),
                     "1728000"),
        "max age");
    check(QByteArray(server1_http_response_header(
              r, "access-control-allow-headers")) ==
              (headers.isEmpty() ? QByteArray("Range") : headers),
          "request headers/default Range");
    server1_http_response_destroy(r);
  }
  for (auto env : {QByteArray("0"), QByteArray("false"), QByteArray("1")}) {
    qputenv("NO_CORS", env);
    r = request("GET", "https://evil.test");
    check(!std::strcmp(
              server1_http_response_header(r, "access-control-allow-origin"),
              "*"),
          "truthy environment bypasses filter");
    server1_http_response_destroy(r);
  }
  qputenv("NO_CORS", "");
  r = request("GET", "https://evil.test");
  check(!*server1_http_response_header(r, "access-control-allow-origin"),
        "empty env not truthy");
  server1_http_response_destroy(r);
  qunsetenv("NO_CORS");
  check(argc == 2, "exact source fixture required");
  QFile cases(argv[1]);
  check(cases.open(QIODevice::ReadOnly), "source fixture open");
  for (auto row : QJsonDocument::fromJson(cases.readAll()).array()) {
    auto value = row.toObject(), input = value["input"].toObject(),
         expected = value["output"].toObject();
    if (input["env"].isNull())
      qunsetenv("NO_CORS");
    else
      qputenv("NO_CORS", input["env"].toString().toUtf8());
    auto *res =
        request(input["method"].toString().toUtf8(),
                input["origin"].isNull() ? QByteArray{}
                                         : input["origin"].toString().toUtf8(),
                input["requestedHeaders"].isNull()
                    ? QByteArray{}
                    : input["requestedHeaders"].toString().toUtf8());
    QJsonObject headers;
    for (size_t i = 0; i < server1_http_response_header_count(res); ++i)
      headers[QString::fromUtf8(server1_http_response_header_name_at(res, i))
                  .toLower()] =
          QString::fromUtf8(server1_http_response_header_value_at(res, i));
    QJsonObject actual{
        {"status", server1_http_response_status(res)},
        {"body", QString::fromUtf8(server1_http_response_body(res),
                                   server1_http_response_body_size(res))},
        {"headers", headers}};
    if (actual != expected) {
      std::cerr << "cors differential mismatch\n"
                << QJsonDocument(value).toJson().constData()
                << QJsonDocument(actual).toJson().constData();
      return 2;
    }
    server1_http_response_destroy(res);
  }
  qunsetenv("NO_CORS");
  server1_http_router_destroy(router);
  std::cout << "external CORS source cases passed\n";
}
