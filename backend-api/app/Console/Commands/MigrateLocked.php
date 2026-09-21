<?php

namespace App\Console\Commands;

use Illuminate\Console\Command;
use Illuminate\Support\Facades\Artisan;
use Illuminate\Support\Facades\DB;
use stdClass;

final class MigrateLocked extends Command
{
    protected $signature = 'telebezel:migrate-locked {--pretend : Show the SQL without running it}';

    protected $description = 'Run explicit migrations under a database-wide PostgreSQL advisory lock';

    public function handle(): int
    {
        if (DB::connection()->getDriverName() !== 'pgsql') {
            $this->error('Production migrations require PostgreSQL.');

            return self::FAILURE;
        }
        // This lock exists before the migrations/cache tables. The connection
        // remains alive in this process while Artisan applies migrations.
        $connection = DB::connection();
        $lock = $connection->selectOne('select pg_try_advisory_lock(7411845081) as acquired');
        if (! in_array($lock instanceof stdClass ? $lock->acquired : null, [true, 't', '1', 1], true)) {
            $this->error('Another TeleBezel migration is already running.');

            return 75;
        }
        try {
            return Artisan::call('migrate', [
                '--force' => true,
                '--pretend' => $this->option('pretend'),
            ]);
        } finally {
            $connection->selectOne('select pg_advisory_unlock(7411845081)');
        }
    }
}
