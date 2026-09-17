# syntax=docker/dockerfile:1.11
FROM debian:13.1-slim@sha256:a347fd7510ee31a84387619a492ad6c8eb0af2f2682b916ff3e643eb076f925a AS build
ARG CMAKE_VERSION=4.4.3
ARG VERSION=0.1.0
ARG TARGETARCH
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential ca-certificates gperf libssl-dev ninja-build php-cli python3-venv zlib1g-dev \
    && python3 -m venv /opt/cmake \
    && /opt/cmake/bin/pip install --no-cache-dir "cmake==${CMAKE_VERSION}" \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN --mount=type=cache,id=telebezel-tdlib-build-${TARGETARCH},target=/src/build \
    test "$VERSION" = "$(cat VERSION)" \
    && \
    /opt/cmake/bin/cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DTELEBEZEL_BUILD_TESTS=ON \
    && /opt/cmake/bin/cmake --build build --target telebezel-tdlib telebezel-tdlib-tests --parallel 2 \
    && /opt/cmake/bin/ctest --test-dir build --output-on-failure \
    && install -D -m 0755 build/telebezel-tdlib /out/telebezel-tdlib

FROM debian:13.1-slim@sha256:a347fd7510ee31a84387619a492ad6c8eb0af2f2682b916ff3e643eb076f925a AS runtime
ARG VERSION=0.1.0
LABEL org.opencontainers.image.source="https://github.com/serogaq/TeleBezel" \
      org.opencontainers.image.title="TeleBezel backend-tdlib" \
      org.opencontainers.image.version="${VERSION}"
RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates libssl3t64 libstdc++6 zlib1g \
    && groupadd --gid 10002 telebezel \
    && useradd --uid 10002 --gid 10002 --no-create-home --home-dir /var/lib/telebezel --shell /usr/sbin/nologin telebezel \
    && mkdir -p /var/lib/telebezel/tdlib \
    && chown -R 10002:10002 /var/lib/telebezel \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /out/telebezel-tdlib /usr/local/bin/telebezel-tdlib
USER 10002:10002
EXPOSE 8081
ENTRYPOINT ["/usr/local/bin/telebezel-tdlib"]
