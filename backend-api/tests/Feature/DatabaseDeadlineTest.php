<?php

use Illuminate\Database\QueryException;
use Illuminate\Support\Facades\Config;
use Illuminate\Support\Facades\DB;

test('stalled postgres handshake returns not ready within budget and recovers', function (): void {
    $server = stream_socket_server('tcp://127.0.0.1:0', $errno, $error);
    expect($server)->toBeResource();
    $address = stream_socket_get_name($server, false);
    expect($address)->toBeString();
    $port = (int) substr($address, strrpos($address, ':') + 1);
    $child = pcntl_fork();
    expect($child)->not->toBe(-1);
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
        expect(microtime(true) - $started)->toBeLessThan(4.5);
    } finally {
        Config::set('database.connections.pgsql', $original);
        DB::purge('pgsql');
        posix_kill($child, SIGTERM);
        pcntl_waitpid($child, $status);
    }
    expect((int) DB::selectOne('select 1 as ready')->ready)->toBe(1);
});
test('server statement timeout is active and connection recovers', function (): void {
    expect(DB::selectOne('show statement_timeout')->statement_timeout)->toBe('2s');
    expect(DB::selectOne('show lock_timeout')->lock_timeout)->toBe('500ms');
    $started = microtime(true);
    try {
        DB::selectOne('select pg_sleep(8)');
        $this->fail('A slow query should be cancelled by PostgreSQL');
    } catch (QueryException) {
        expect(microtime(true) - $started)->toBeLessThan(3.5);
    }
    expect((int) DB::selectOne('select 1 as ready')->ready)->toBe(1);
});
test('configured timeouts are applied to every new connection', function (): void {
    $original = Config::get('database.connections.pgsql');
    try {
        Config::set('database.connections.pgsql.statement_timeout', 1500);
        Config::set('database.connections.pgsql.lock_timeout', '250');
        DB::purge('pgsql');
        expect(DB::selectOne('show statement_timeout')->statement_timeout)->toBe('1500ms');
        expect(DB::selectOne('show lock_timeout')->lock_timeout)->toBe('250ms');
    } finally {
        Config::set('database.connections.pgsql', $original);
        DB::purge('pgsql');
    }
    expect(DB::selectOne('show statement_timeout')->statement_timeout)->toBe('2s');
});
test('an invalid timeout is refused instead of reaching sql', function (): void {
    $original = Config::get('database.connections.pgsql');
    try {
        Config::set('database.connections.pgsql.statement_timeout', '1; drop table migrations');
        DB::purge('pgsql');
        try {
            DB::selectOne('select 1');
            $this->fail('An invalid timeout reached the database');
        } catch (QueryException $exception) {
            expect($exception->getPrevious())->toBeInstanceOf(InvalidArgumentException::class);
        }
    } finally {
        Config::set('database.connections.pgsql', $original);
        DB::purge('pgsql');
    }
    expect((int) DB::selectOne('select 1 as ready')->ready)->toBe(1);
});
