<?php

declare(strict_types=1);

namespace App\Console\Commands;

use App\Contracts\Repositories\RetentionRepository;
use Illuminate\Console\Command;

final class PurgeExpired extends Command
{
    protected $signature = 'telebezel:purge-expired';

    protected $description = 'Delete expired web sessions, bootstrap codes, idempotency keys and send records';

    public function handle(RetentionRepository $retention): int
    {
        foreach ($retention->purge(now()->toImmutable()) as $table => $count) {
            $this->line($table.': '.$count);
        }

        return self::SUCCESS;
    }
}
