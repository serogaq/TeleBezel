<?php

declare(strict_types=1);

require dirname(__DIR__, 2).'/backend-api/vendor/autoload.php';

$app = require dirname(__DIR__, 2).'/backend-api/bootstrap/app.php';
$app->make(Illuminate\Contracts\Console\Kernel::class)->bootstrap();

$expectedDatabase = getenv('TELEBEZEL_DISPOSABLE_DB');
$expectedPort = getenv('TELEBEZEL_DISPOSABLE_PORT');
$connection = Illuminate\Support\Facades\DB::connection();
$config = $connection->getConfig();

if (!is_string($expectedDatabase) || !preg_match('/^telebezel_disposable_[0-9]+_[0-9]+$/', $expectedDatabase)
    || !is_string($expectedPort) || !ctype_digit($expectedPort)
    || $connection->getDriverName() !== 'pgsql'
    || config('database.default') !== 'pgsql'
    || ($config['host'] ?? null) !== '127.0.0.1'
    || (string) ($config['port'] ?? '') !== $expectedPort
    || ($config['database'] ?? null) !== $expectedDatabase
    || !in_array($config['url'] ?? null, [null, ''], true)
    || file_exists(dirname(__DIR__, 2).'/backend-api/bootstrap/cache/config.php')) {
    fwrite(STDERR, "Refusing destructive migration: database configuration is not the disposable PostgreSQL container\n");
    exit(1);
}

$actual = $connection->selectOne('select current_database() as name, inet_server_port() as port');
if ($actual?->name !== $expectedDatabase || (string) $actual?->port !== '5432') {
    fwrite(STDERR, "Refusing destructive migration: connected server does not match the disposable PostgreSQL container\n");
    exit(1);
}
