FROM golang:1.23-alpine AS build
WORKDIR /src
COPY go.mod ./
COPY main.go ./
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /out/trakt-token-broker .

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /out/trakt-token-broker /trakt-token-broker
USER nonroot:nonroot
EXPOSE 8080
ENTRYPOINT ["/trakt-token-broker"]
