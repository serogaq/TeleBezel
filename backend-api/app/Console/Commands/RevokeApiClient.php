<?php

namespace App\Console\Commands;

use App\Services\AdministrationService;
use Illuminate\Console\Command;
use Illuminate\Support\Str;

final class RevokeApiClient extends Command
{
    protected $signature = 'telebezel:api-client-revoke {id}';

    protected $description = 'Revoke a TeleBezel API token';

    public function handle(AdministrationService $service): int
    {
        if (! Str::isUuid($this->argument('id'))) {
            $this->error('Token ID must be a UUID.');

            return self::FAILURE;
        }
        if (! $service->revoke($this->argument('id'))) {
            $this->error('Token not found.');

            return self::FAILURE;
        }
        $this->info('Token revoked.');

        return self::SUCCESS;
    }
}
