<?php

namespace App\Console\Commands;

use App\Models\ApiClient;
use Illuminate\Console\Command;

final class IssueApiClient extends Command
{
    protected $signature = 'telebezel:api-client-issue {name}';

    protected $description = 'Issue a TeleBezel API client token';

    public function handle(): int
    {
        $name = trim((string) $this->argument('name'));
        if ($name === '' || mb_strlen($name) > 100) {
            $this->error('Name must contain between 1 and 100 characters.');

            return self::FAILURE;
        }

        $token = 'tb_'.rtrim(strtr(base64_encode(random_bytes(32)), '+/', '-_'), '=');
        $client = ApiClient::query()->create([
            'name' => $name,
            'token_prefix' => substr($token, 0, 12),
            'token_hash' => hash('sha256', $token),
        ]);

        $this->warn('Store this token now; it cannot be shown again.');
        $this->line($token);
        $this->comment('Client ID: '.$client->getKey());

        return self::SUCCESS;
    }
}
