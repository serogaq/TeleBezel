# syntax=docker/dockerfile:1.11
FROM golang:1.27.1-trixie@sha256:433790e515d27dc6003e847e644cc0af956985cf315c1c58a3b73ee2dd305183 AS build
ARG VERSION=0.1.0
WORKDIR /src
COPY go.mod ./
COPY cmd/ cmd/
COPY internal/ internal/
COPY VERSION ./
RUN test "$VERSION" = "$(cat VERSION)" \
    && go vet ./... \
    && go test ./... \
    && CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /out/telebezel-media ./cmd/telebezel-media

FROM scratch AS runtime
ARG VERSION=0.1.0
LABEL org.opencontainers.image.source="https://github.com/serogaq/TeleBezel" \
      org.opencontainers.image.title="TeleBezel backend-media" \
      org.opencontainers.image.version="${VERSION}"
COPY --from=build /out/telebezel-media /telebezel-media
USER 10003:10003
EXPOSE 8082
ENTRYPOINT ["/telebezel-media"]
