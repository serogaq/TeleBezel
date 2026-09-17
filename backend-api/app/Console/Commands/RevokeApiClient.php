<?php

namespace App\Console\Commands;

use App\Models\ApiClient;
use Illuminate\Console\Command;
use Illuminate\Support\Str;

final class RevokeApiClient extends Command
{
    protected $signature = 'telebezel:api-client-revoke {id}';

    protected $description = 'Revoke a TeleBezel API client token';

    public function handle(): int
    {
        if (! Str::isUuid((string) $this->argument('id'))) {
            $this->error('Client ID must be a UUID.');

            return self::FAILURE;
        }
        $client = ApiClient::query()->find($this->argument('id'));
        if ($client === null) {
            $this->error('API client not found.');

            return self::FAILURE;
        }
        $client->forceFill(['revoked_at' => now()])->save();
        $this->info('API client revoked.');

        return self::SUCCESS;
    }
}
