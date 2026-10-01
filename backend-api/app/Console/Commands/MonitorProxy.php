<?php

namespace App\Console\Commands;

use App\Services\ProxyProfileService;
use Illuminate\Console\Command;
use Illuminate\Support\Str;

final class MonitorProxy extends Command
{
    protected $signature = 'telebezel:proxy-monitor';

    protected $description = 'Monitor the active proxy, apply the configured failover policy and print the seconds until the next check';

    public function handle(ProxyProfileService $profiles): int
    {
        try {
            $profiles->monitor((string) Str::uuid());
        } finally {
            $this->line((string) $profiles->monitorInterval());
        }

        return self::SUCCESS;
    }
}
