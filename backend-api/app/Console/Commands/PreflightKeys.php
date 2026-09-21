<?php

namespace App\Console\Commands;

use App\Models\Instance;
use Illuminate\Console\Command;
use Illuminate\Contracts\Encryption\DecryptException;

final class PreflightKeys extends Command
{
    protected $signature = 'telebezel:preflight-keys';

    protected $description = 'Verify APP_KEY control data without exposing encrypted configuration';

    public function handle(): int
    {
        try {
            $instance = Instance::query()->first();
            if ($instance === null || $instance->owner_password_hash === null) {
                $this->components->info('APP_KEY check deferred until owner bootstrap.');

                return self::SUCCESS;
            }
            if ($instance->app_key_check !== 'telebezel-app-key-v1:'.$instance->id) {
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
