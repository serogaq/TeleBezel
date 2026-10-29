# syntax=docker/dockerfile:1.11
FROM composer:2.10.3@sha256:d8f6343d3fae98107426bc49163ccad46ef85aabd4a27d80a74401fab4aba332 AS composer-bin
FROM dunglas/frankenphp:1.13.1-builder-php8.5-bookworm@sha256:39144ec2ce0cc782c7daa6c14f78651790d36fd59842efa233c6e8b09cf901a4 AS frankenphp-builder
# The upstream 1.12.7 binary predates these fixed Go dependencies. Rebuild
# the same release against explicit patched module versions until upstream ships
# a release containing them.
WORKDIR /go/src/app/caddy
RUN go get github.com/getkin/kin-openapi@v0.144.0 golang.org/x/crypto@v0.55.0 google.golang.org/grpc@v1.83.2
WORKDIR /go/src/app/caddy/frankenphp
RUN GOBIN=/usr/local/bin ../../go.sh install -ldflags="-w -s -X 'github.com/caddyserver/caddy/v2.CustomVersion=FrankenPHP 1.12.7 PHP 8.5 Caddy' -X 'github.com/caddyserver/caddy/v2.CustomBinaryName=frankenphp' -X 'github.com/caddyserver/caddy/v2/modules/caddyhttp.ServerHeader=FrankenPHP Caddy'" -buildvcs=false

FROM dunglas/frankenphp:1.13.2-php8.5-bookworm@sha256:308b7dbf7e263a7200f17c1957c0ed1dfdcd01fd914e1b7e99eef7f92b06d4ae AS build
COPY --from=composer-bin /usr/bin/composer /usr/local/bin/composer
RUN install-php-extensions pdo_pgsql pcntl zip intl
WORKDIR /app
COPY composer.json composer.lock ./
RUN composer install --no-dev --no-interaction --no-progress --prefer-dist --no-scripts --no-autoloader
COPY . .
RUN composer dump-autoload --no-dev --classmap-authoritative --no-interaction

FROM dunglas/frankenphp:1.13.2-php8.5-bookworm@sha256:308b7dbf7e263a7200f17c1957c0ed1dfdcd01fd914e1b7e99eef7f92b06d4ae AS runtime
ARG VERSION=0.3.0
LABEL org.opencontainers.image.source="https://github.com/serogaq/TeleBezel" \
      org.opencontainers.image.title="TeleBezel backend-api" \
      org.opencontainers.image.version="${VERSION}"
COPY --from=frankenphp-builder /usr/local/bin/frankenphp /usr/local/bin/frankenphp
RUN apt-get update && apt-get upgrade -y --no-install-recommends \
    && rm -rf /var/lib/apt/lists/* \
    && install-php-extensions pdo_pgsql pcntl intl \
    && groupadd --gid 10001 telebezel \
    && useradd --uid 10001 --gid 10001 --no-create-home --home-dir /app --shell /usr/sbin/nologin telebezel \
    && mkdir -p /app/storage /app/bootstrap/cache /config /data \
    && chown -R 10001:10001 /app /config /data
WORKDIR /app
COPY --from=build --chown=10001:10001 /app /app
RUN test "$VERSION" = "$(cat /app/VERSION)"
USER 10001:10001
EXPOSE 8080
ENTRYPOINT ["/app/docker/entrypoint.sh"]
CMD ["php", "artisan", "octane:frankenphp", "--host=0.0.0.0", "--port=8080", "--workers=2", "--max-requests=500"]
