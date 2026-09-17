<?php

namespace Tests\Feature;

use Illuminate\Support\Facades\Artisan;
use Illuminate\Support\Facades\Config;
use Illuminate\Support\Facades\DB;
use Tests\TestCase;

final class MigrationLockTest extends TestCase
{
    public function test_independent_postgres_connection_blocks_a_second_migration_runner(): void
    {
        Config::set('database.connections.pgsql_lock_holder', Config::get('database.connections.pgsql'));
        $holder = DB::connection('pgsql_lock_holder');
        $holder->selectOne('select pg_advisory_lock(7411845081)');
        try {
            $this->assertSame(75, Artisan::call('telebezel:migrate-locked'));
        } finally {
            $holder->selectOne('select pg_advisory_unlock(7411845081)');
            DB::purge('pgsql_lock_holder');
            Config::set('database.connections.pgsql_lock_holder', null);
        }
        $this->assertSame(0, Artisan::call('telebezel:migrate-locked'));
    }
}
