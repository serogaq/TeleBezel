<?php

namespace Tests\Feature;

use Illuminate\Database\QueryException;
use Illuminate\Support\Facades\Config;
use Illuminate\Support\Facades\DB;
use Tests\TestCase;

final class DatabaseDeadlineTest extends TestCase
{
    public function test_stalled_postgres_handshake_returns_not_ready_within_budget_and_recovers(): void
    {
        $server = stream_socket_server('tcp://127.0.0.1:0', $errno, $error);
        $this->assertIsResource($server);
        $address = stream_socket_get_name($server, false);
        $this->assertIsString($address);
        $port = (int) substr($address, strrpos($address, ':') + 1);
        $child = pcntl_fork();
        $this->assertNotSame(-1, $child);
        if ($child === 0) {
            $socket = stream_socket_accept($server, 5);
            if ($socket !== false) {
                sleep(8);
                fclose($socket);
            }
            exit(0);
        }

        fclose($server);
        $original = Config::get('database.connections.pgsql');
        DB::purge('pgsql');
        Config::set('database.connections.pgsql.port', $port);
        try {
            $started = microtime(true);
            $this->get('/readyz')->assertStatus(503)->assertJsonPath('status', 'not_ready');
            $this->assertLessThan(4.5, microtime(true) - $started);
        } finally {
            Config::set('database.connections.pgsql', $original);
            DB::purge('pgsql');
            posix_kill($child, SIGTERM);
            pcntl_waitpid($child, $status);
        }
        $this->assertSame(1, (int) DB::selectOne('select 1 as ready')->ready);
    }

    public function test_server_statement_timeout_is_active_and_connection_recovers(): void
    {
        $this->assertSame('2s', DB::selectOne('show statement_timeout')->statement_timeout);
        $this->assertSame('500ms', DB::selectOne('show lock_timeout')->lock_timeout);
        $started = microtime(true);
        try {
            DB::selectOne('select pg_sleep(8)');
            $this->fail('A slow query should be cancelled by PostgreSQL');
        } catch (QueryException) {
            $this->assertLessThan(3.5, microtime(true) - $started);
        }
        $this->assertSame(1, (int) DB::selectOne('select 1 as ready')->ready);
    }
}
