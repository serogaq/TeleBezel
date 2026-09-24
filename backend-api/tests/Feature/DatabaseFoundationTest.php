<?php

use Illuminate\Support\Facades\Cache;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Schema;

test('only the foundational tables are present', function (): void {
    expect(Schema::hasTable('access_tokens'))->toBeTrue();
    expect(Schema::hasTable('api_clients'))->toBeFalse();
    expect(Schema::hasTable('owner_sessions'))->toBeFalse();
    expect(Schema::hasTable('cache'))->toBeTrue();
    expect(Schema::hasTable('cache_locks'))->toBeTrue();
    expect(Schema::hasTable('users'))->toBeFalse();
    expect(Schema::hasTable('jobs'))->toBeFalse();
});
test('postgres driver server and database isolation are real', function (): void {
    expect(DB::connection()->getDriverName())->toBe('pgsql');
    $server = DB::selectOne("select current_database() as database, current_setting('server_version_num')::int as version");
    expect($server->database)->toBe(config('database.connections.pgsql.database'));
    expect((int) $server->version)->toBeGreaterThanOrEqual(180000);
    expect((int) $server->version)->toBeLessThan(190000);
});
test('database cache locks are shared', function (): void {
    $name = 'stage0-cache-lock-test-'.bin2hex(random_bytes(8));
    $first = Cache::lock($name, 10);
    $second = Cache::lock($name, 10);
    try {
        expect($first->get())->toBeTrue();
        expect($second->get())->toBeFalse();
        $first->release();
        expect($second->get())->toBeTrue();
    } finally {
        $first->release();
        $second->release();
    }
});
