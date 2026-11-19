# syntax=docker/dockerfile:1.11
FROM golang:1.27.3-trixie@sha256:af1bfc2614ebb1dbd31af514b78740fd678850e76498762b425ecee4c5c6ea80 AS build
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
