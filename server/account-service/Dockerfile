FROM golang:1.26.6-bookworm AS build

WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download

COPY . .
RUN CGO_ENABLED=0 GOOS=linux go build -trimpath -ldflags="-s -w" \
    -o /out/colosseum-account-service ./cmd/colosseum-account-service
RUN CGO_ENABLED=0 GOOS=linux go build -trimpath -ldflags="-s -w" \
    -o /out/colosseum-account-migrate ./cmd/colosseum-account-migrate
RUN CGO_ENABLED=0 GOOS=linux go build -trimpath -ldflags="-s -w" \
    -o /out/colosseum-account-maintenance ./cmd/colosseum-account-maintenance

FROM gcr.io/distroless/static-debian12:nonroot

COPY --from=build /out/colosseum-account-service /colosseum-account-service
COPY --from=build /out/colosseum-account-migrate /colosseum-account-migrate
COPY --from=build /out/colosseum-account-maintenance /colosseum-account-maintenance

USER nonroot:nonroot
EXPOSE 8080

ENTRYPOINT ["/colosseum-account-service"]
