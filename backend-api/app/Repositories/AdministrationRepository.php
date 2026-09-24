<?php

declare(strict_types=1);

namespace App\Repositories;

use App\Contracts\Repositories\AdministrationRepository as AdministrationRepositoryContract;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Str;

final class AdministrationRepository implements AdministrationRepositoryContract
{
    public function bootstrap(string $hash): void
    {
        DB::table('bootstrap_codes')->insert([
            'id' => (string) Str::uuid(),
            'code_hash' => $hash,
            'expires_at' => now()->addDay(),
            'created_at' => now(),
            'updated_at' => now(),
        ]);
    }
}
