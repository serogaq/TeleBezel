<?php

namespace App\Console\Commands;

use App\Services\AdministrationService;
use Illuminate\Console\Command;

final class IssueApiClient extends Command
{
    protected $signature = 'telebezel:api-client-issue {name}';

    protected $description = 'Issue a TeleBezel API client token';

    public function handle(AdministrationService $service): int
    {
        $name = trim($this->argument('name'));
        if ($name === '' || mb_strlen($name) > 100) {
            $this->error('Name must contain between 1 and 100 characters.');

            return self::FAILURE;
        }
        $issued = $service->issue($name);
        $token = $issued['token'];
        $this->warn('Store this token now; it cannot be shown again.');
        $this->line($token);
        $this->comment('Client ID: '.$issued['id']);

        return self::SUCCESS;
    }
}
