<?php

namespace App\Console\Commands;

use App\Services\AdministrationService;
use Illuminate\Console\Command;
use Illuminate\Contracts\Encryption\DecryptException;

final class PreflightKeys extends Command
{
    protected $signature = 'telebezel:preflight-keys';

    protected $description = 'Verify APP_KEY control data without exposing encrypted configuration';

    public function handle(AdministrationService $service): int
    {
        try {
            $status = $service->keyStatus();
            if ($status === 'deferred') {
                $this->components->info('APP_KEY check deferred until owner bootstrap.');

                return self::SUCCESS;
            }
            if ($status !== 'valid') {
                $this->components->error('APP_KEY control data is missing or invalid; restore the original APP_KEY.');

                return self::FAILURE;
            }
        } catch (DecryptException) {
            $this->components->error('APP_KEY cannot decrypt control data; restore the original APP_KEY.');

            return self::FAILURE;
        }
        $this->components->info('APP_KEY control data verified.');

        return self::SUCCESS;
    }
}
