<?php

namespace App\Console\Commands;

use App\Models\Instance;
use Illuminate\Console\Command;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Str;

final class CreateBootstrapCode extends Command
{
    protected $signature = 'telebezel:bootstrap-code';

    protected $description = 'Create the one-time owner bootstrap code for a fresh installation';

    public function handle(): int
    {
        if (Instance::query()->whereNotNull('owner_password_hash')->exists()) {
            $this->components->error('The owner already exists; bootstrap cannot be reopened.');

            return self::FAILURE;
        }
        $code = strtoupper(Str::random(8).'-'.Str::random(8));
        DB::table('bootstrap_codes')->insert(['id' => (string) Str::uuid(), 'code_hash' => hash('sha256', $code),
            'expires_at' => now()->addDay(), 'created_at' => now(), 'updated_at' => now()]);
        $this->components->info('Bootstrap code: '.$code);
        $this->line('It expires in 24 hours and is shown only in this terminal.');

        return self::SUCCESS;
    }
}
