<?php

namespace App\Console\Commands;

use App\Services\ProxyProfileService;
use Illuminate\Console\Command;
use Illuminate\Support\Str;

final class MonitorProxy extends Command
{
    protected $signature = 'telebezel:proxy-monitor';

    protected $description = 'Monitor the active proxy and apply the configured failover policy';

    public function handle(ProxyProfileService $profiles): int
    {
        $profiles->monitor((string) Str::uuid());

        return self::SUCCESS;
    }
}
