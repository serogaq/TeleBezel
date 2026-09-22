<?php

use Illuminate\Support\Facades\Artisan;
use Illuminate\Support\Facades\Config;
use Illuminate\Support\Facades\DB;

test('independent postgres connection blocks a second migration runner', function (): void {
    Config::set('database.connections.pgsql_lock_holder', Config::get('database.connections.pgsql'));
    $holder = DB::connection('pgsql_lock_holder');
    $holder->selectOne('select pg_advisory_lock(7411845081)');
    try {
        expect(Artisan::call('telebezel:migrate-locked'))->toBe(75);
    } finally {
        $holder->selectOne('select pg_advisory_unlock(7411845081)');
        DB::purge('pgsql_lock_holder');
        Config::set('database.connections.pgsql_lock_holder', null);
    }
    expect(Artisan::call('telebezel:migrate-locked'))->toBe(0);
});
