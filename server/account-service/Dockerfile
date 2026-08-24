FROM golang:1.26.6-bookworm AS build

WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download

COPY . .
RUN CGO_ENABLED=0 GOOS=linux go build -trimpath -ldflags="-s -w" \
    -o /out/colosseum-account-service ./cmd/colosseum-account-service

FROM gcr.io/distroless/static-debian12:nonroot

COPY --from=build /out/colosseum-account-service /colosseum-account-service

USER nonroot:nonroot
EXPOSE 8080

ENTRYPOINT ["/colosseum-account-service"]
