<?php

declare(strict_types=1);

namespace App\Console\Commands;

use App\Services\ReconciliationService;
use Illuminate\Console\Command;

final class ReconcileAccounts extends Command
{
    protected $signature = 'telebezel:accounts-reconcile {--dry-run : Report desired accounts without activating clients}';

    protected $description = 'Reconcile durable Telegram account intent with the TDLib runtime';

    public function handle(ReconciliationService $service): int
    {
        foreach ($service->run($this->option('dry-run')) as $line) {
            $this->line($line);
        }

        return self::SUCCESS;
    }
}
