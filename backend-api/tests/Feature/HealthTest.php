<?php

use Illuminate\Support\Facades\Config;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Http;

test('liveness does not require dependencies', function (): void {
    $this->getJson('/healthz')->assertOk()->assertHeader('X-Request-ID')->assertExactJson([
        'status' => 'ok',
        'service' => 'backend-api',
        'version' => config()->string('telebezel.version'),
    ]);
});
test('readiness checks postgres and tdlib', function (): void {
    Http::fake([
        '*' => Http::response(tdlibReadyStatusFixture()),
    ]);
    $this->getJson('/readyz')->assertOk()->assertExactJson([
        'status' => 'ready',
    ]);
    Http::assertSent(fn ($request): bool => $request->hasHeader('Authorization', 'Bearer test-internal-token'));
});
test('readiness is unavailable when postgres is down', function (): void {
    $original = Config::get('database.connections.pgsql');
    DB::purge('pgsql');
    Config::set('database.connections.pgsql.port', 1);
    try {
        $this->getJson('/readyz')->assertStatus(503)->assertExactJson([
            'status' => 'not_ready',
        ]);
    } finally {
        Config::set('database.connections.pgsql', $original);
        DB::purge('pgsql');
    }
});
