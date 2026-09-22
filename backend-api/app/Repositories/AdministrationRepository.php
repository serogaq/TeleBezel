<?php

declare(strict_types=1);

namespace App\Repositories;

use App\Contracts\Repositories\AdministrationRepository as AdministrationRepositoryContract;
use App\Models\ApiClient;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Str;

final class AdministrationRepository implements AdministrationRepositoryContract
{
    public function issue(string $name, string $hash, string $prefix): string
    {
        return ApiClient::query()->create([
            'name' => $name,
            'token_hash' => $hash,
            'token_prefix' => $prefix,
        ])->id;
    }

    public function revoke(string $id): bool
    {
        $client = ApiClient::query()->find($id);
        if ($client === null) {
            return false;
        }
        $client->forceFill([
            'revoked_at' => now(),
        ])->save();

        return true;
    }

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
