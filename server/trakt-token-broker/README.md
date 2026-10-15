# Colosseum Trakt Token Broker

This service exists only because Trakt's confidential client secret must not ship in the desktop application.

It exposes exactly three POST operations:

- /v1/trakt/device-token
- /v1/trakt/refresh
- /v1/trakt/revoke

Normal Trakt history, playback, identity and scrobble API calls remain direct from Colosseum to Trakt.

## Environment

- TRAKT_CLIENT_ID
- TRAKT_CLIENT_SECRET
- TRAKT_REDIRECT_URI
- LISTEN_ADDR, default :8080
- TRUST_PROXY, default false
- RATE_LIMIT_PER_MINUTE, default 60

Deploy behind HTTPS. If TRUST_PROXY is enabled, configure it only behind a proxy that overwrites X-Forwarded-For.

The broker deliberately does not log request bodies or OAuth responses.

OAuth exchanges use the fixed `https://auth.trakt.tv` endpoints. Redirects are refused, failures return sanitized errors, and successful token responses include only the token schema. The client cannot select an upstream URL. `go test ./...` exercises fake local upstreams without provider credentials.
