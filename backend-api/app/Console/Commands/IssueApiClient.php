<?php

namespace App\Console\Commands;

use App\Enums\TokenType;
use App\Services\AdministrationService;
use Illuminate\Console\Command;

final class IssueApiClient extends Command
{
    protected $signature = 'telebezel:api-client-issue {name} {--type=maintenance : device or maintenance}';

    protected $description = 'Issue a TeleBezel API token';

    public function handle(AdministrationService $service): int
    {
        $name = trim($this->argument('name'));
        if ($name === '' || mb_strlen($name) > 100) {
            $this->error('Name must contain between 1 and 100 characters.');

            return self::FAILURE;
        }
        $type = TokenType::tryFrom((string) $this->option('type'));
        if ($type === null) {
            $this->error('Type must be device or maintenance.');

            return self::FAILURE;
        }
        $issued = $service->issue($name, $type);
        $this->warn('Store this token now; it cannot be shown again.');
        $this->line($issued['token']);
        $this->comment('Token ID: '.$issued['id']);
        $this->comment('Permissions: '.implode(', ', $type->permissionNames()));

        return self::SUCCESS;
    }
}
