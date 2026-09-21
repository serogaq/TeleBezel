<?php

namespace App\Console\Commands;

use App\Services\AdministrationService;
use Illuminate\Console\Command;
use Illuminate\Support\Str;

final class RevokeApiClient extends Command
{
    protected $signature = 'telebezel:api-client-revoke {id}';

    protected $description = 'Revoke a TeleBezel API client token';

    public function handle(AdministrationService $service): int
    {
        if (! Str::isUuid($this->argument('id'))) {
            $this->error('Client ID must be a UUID.');

            return self::FAILURE;
        }
        if (! $service->revoke($this->argument('id'))) {
            $this->error('API client not found.');

            return self::FAILURE;
        }
        $this->info('API client revoked.');

        return self::SUCCESS;
    }
}
