# syntax=docker/dockerfile:1.11
FROM composer:2.10.3@sha256:d8f6343d3fae98107426bc49163ccad46ef85aabd4a27d80a74401fab4aba332 AS composer-bin
FROM dunglas/frankenphp:1.12.7-php8.5-bookworm@sha256:7380f2c24ef9d86ad496dbc285ba66e212365c864db2797f3e7269afad100dbb AS build
COPY --from=composer-bin /usr/bin/composer /usr/local/bin/composer
RUN install-php-extensions pdo_pgsql pcntl zip
WORKDIR /app
COPY composer.json composer.lock ./
RUN composer install --no-dev --no-interaction --no-progress --prefer-dist --no-scripts --no-autoloader
COPY . .
RUN composer dump-autoload --no-dev --classmap-authoritative --no-interaction

FROM dunglas/frankenphp:1.12.7-php8.5-bookworm@sha256:7380f2c24ef9d86ad496dbc285ba66e212365c864db2797f3e7269afad100dbb AS runtime
ARG VERSION=0.1.0
LABEL org.opencontainers.image.source="https://github.com/serogaq/TeleBezel" \
      org.opencontainers.image.title="TeleBezel backend-api" \
      org.opencontainers.image.version="${VERSION}"
RUN install-php-extensions pdo_pgsql pcntl \
    && setcap -r /usr/local/bin/frankenphp \
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
