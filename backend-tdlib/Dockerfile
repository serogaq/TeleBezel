# syntax=docker/dockerfile:1.11
FROM debian:13.6-slim@sha256:d7e12182ce18b85b93007c1dedf31f2d29e01ccf3182cc4017c709b6259bc132 AS build
ARG CMAKE_VERSION=4.4.3
ARG VERSION=0.3.0
ARG TARGETARCH
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential ca-certificates ccache gperf libssl-dev ninja-build php-cli python3-venv zlib1g-dev \
    && python3 -m venv /opt/cmake \
    && /opt/cmake/bin/pip install --no-cache-dir "cmake==${CMAKE_VERSION}" \
    && rm -rf /var/lib/apt/lists/*
FROM build AS compiled
WORKDIR /src
COPY CMakeLists.txt VERSION ./
COPY cmake/ cmake/
COPY include/ include/
COPY src/ src/
COPY tests/ tests/
ENV CCACHE_DIR=/var/cache/ccache \
    CCACHE_COMPRESS=1 \
    CCACHE_MAXSIZE=1500M
RUN --mount=type=cache,id=telebezel-tdlib-build-${TARGETARCH},target=/src/build,sharing=locked \
    --mount=type=cache,id=telebezel-tdlib-ccache-${TARGETARCH},target=/var/cache/ccache \
    test "$VERSION" = "$(cat VERSION)" \
    && ccache --zero-stats \
    && /opt/cmake/bin/cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DTELEBEZEL_BUILD_TESTS=ON -DTELEBEZEL_SANITIZER=none \
    && /opt/cmake/bin/cmake --build build --target telebezel-tdlib telebezel-tdlib-tests telebezel-tdlib-runtime-tests telebezel-tdlib-failure-tests --parallel 2 \
    && /opt/cmake/bin/ctest --test-dir build --output-on-failure \
    && install -D -m 0755 build/telebezel-tdlib /out/telebezel-tdlib \
    && ccache --show-stats

# Sanitizers need no additional packages and can run independently of analyzers.
FROM compiled AS sanitizer-check
ARG TARGETARCH
RUN --mount=type=cache,id=telebezel-tdlib-build-${TARGETARCH},target=/src/build,sharing=locked \
    --mount=type=cache,id=telebezel-tdlib-ccache-${TARGETARCH},target=/var/cache/ccache \
    bash tests/sanitizers.sh build /opt/cmake/bin/cmake

# Optional Linux-only quality target; never part of the shipped runtime image.
FROM build AS quality-tools
RUN apt-get update && apt-get install -y --no-install-recommends clang-tidy cppcheck && rm -rf /var/lib/apt/lists/*
FROM quality-tools AS quality
WORKDIR /src
COPY --from=compiled /src /src
ENV CCACHE_DIR=/var/cache/ccache CCACHE_COMPRESS=1 CCACHE_MAXSIZE=1500M
RUN --mount=type=cache,id=telebezel-tdlib-build-${TARGETARCH},target=/src/build,sharing=locked \
    --mount=type=cache,id=telebezel-tdlib-ccache-${TARGETARCH},target=/var/cache/ccache \
    /opt/cmake/bin/cmake -S . -B build -DTELEBEZEL_SANITIZER=none \
    && python3 tests/analyse.py build \
    && bash tests/sanitizers.sh build /opt/cmake/bin/cmake


FROM debian:13.6-slim@sha256:d7e12182ce18b85b93007c1dedf31f2d29e01ccf3182cc4017c709b6259bc132 AS runtime
ARG VERSION=0.3.0
LABEL org.opencontainers.image.source="https://github.com/serogaq/TeleBezel" \
      org.opencontainers.image.title="TeleBezel backend-tdlib" \
      org.opencontainers.image.version="${VERSION}"
RUN apt-get update \
    && apt-get upgrade -y --no-install-recommends \
    && apt-get install -y --no-install-recommends ca-certificates libssl3t64 libstdc++6 zlib1g \
    && groupadd --gid 10002 telebezel \
    && useradd --uid 10002 --gid 10002 --no-create-home --home-dir /var/lib/telebezel --shell /usr/sbin/nologin telebezel \
    && mkdir -p /var/lib/telebezel/tdlib \
    && chown -R 10002:10002 /var/lib/telebezel \
    && rm -rf /var/lib/apt/lists/*
COPY --from=compiled /out/telebezel-tdlib /usr/local/bin/telebezel-tdlib
USER 10002:10002
EXPOSE 8081
ENTRYPOINT ["/usr/local/bin/telebezel-tdlib"]
