<?php

namespace Tests\Feature;

use Illuminate\Support\Facades\Cache;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Schema;
use Tests\TestCase;

final class DatabaseFoundationTest extends TestCase
{
    public function test_only_the_foundational_tables_are_present(): void
    {
        $this->assertTrue(Schema::hasTable('api_clients'));
        $this->assertTrue(Schema::hasTable('cache'));
        $this->assertTrue(Schema::hasTable('cache_locks'));
        $this->assertFalse(Schema::hasTable('users'));
        $this->assertFalse(Schema::hasTable('jobs'));
    }

    public function test_postgres_driver_server_and_database_isolation_are_real(): void
    {
        $this->assertSame('pgsql', DB::connection()->getDriverName());
        $server = DB::selectOne("select current_database() as database, current_setting('server_version_num')::int as version");
        $this->assertSame(config('database.connections.pgsql.database'), $server->database);
        $this->assertGreaterThanOrEqual(180000, (int) $server->version);
        $this->assertLessThan(190000, (int) $server->version);
    }

    public function test_database_cache_locks_are_shared(): void
    {
        $name = 'stage0-cache-lock-test-'.bin2hex(random_bytes(8));
        $first = Cache::lock($name, 10);
        $second = Cache::lock($name, 10);
        try {
            $this->assertTrue($first->get());
            $this->assertFalse($second->get());
            $first->release();
            $this->assertTrue($second->get());
        } finally {
            $first->release();
            $second->release();
        }
    }
}
