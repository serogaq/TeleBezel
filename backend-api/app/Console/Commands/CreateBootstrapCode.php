<?php

namespace App\Console\Commands;

use App\Exceptions\ApiException;
use App\Services\AdministrationService;
use Illuminate\Console\Command;

final class CreateBootstrapCode extends Command
{
    protected $signature = 'telebezel:bootstrap-code';

    protected $description = 'Create the one-time owner bootstrap code for a fresh installation';

    public function handle(AdministrationService $service): int
    {
        try {
            $code = $service->bootstrap();
        } catch (ApiException) {
            $this->components->error('The owner already exists; bootstrap cannot be reopened.');

            return self::FAILURE;
        }
        $this->components->info('Bootstrap code: '.$code);
        $this->line('It expires in 24 hours and is shown only in this terminal.');

        return self::SUCCESS;
    }
}
