# syntax=docker/dockerfile:1.11
FROM golang:1.27.2-trixie@sha256:47667934874913a449b96faa7ef4777fa285d9ec29126ee80ed61c45b7cccf0e AS build
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
