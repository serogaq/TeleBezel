<?php

declare(strict_types=1);

namespace App\Repositories;

use App\Contracts\Repositories\OwnerAccessRepository as OwnerAccessRepositoryContract;
use App\Data\BootstrapCodeData;
use App\Data\OwnerCredentials;
use App\Models\Instance;
use App\Support\Values;
use Carbon\CarbonImmutable;
use Illuminate\Support\Facades\DB;

final class OwnerAccessRepository implements OwnerAccessRepositoryContract
{
    public function lockBootstrap(string $codeHash): ?BootstrapCodeData
    {
        $entry = DB::table('bootstrap_codes')->where('code_hash', $codeHash)->lockForUpdate()->first();

        return $entry === null ? null : new BootstrapCodeData(Values::string($entry->id), CarbonImmutable::parse(Values::string($entry->expires_at)), $entry->consumed_at !== null);
    }

    public function lockOwner(): ?OwnerCredentials
    {
        // Serializes first bootstrap too: a row lock cannot lock a missing instance.
        DB::select('SELECT pg_advisory_xact_lock(?)', [74123011]);
        $instance = Instance::query()->lockForUpdate()->first();

        return $instance === null ? null : new OwnerCredentials($instance->id, $instance->owner_password_hash, $instance->recovery_code_hash, $instance->app_key_check);
    }

    public function createInstance(string $id): void
    {
        Instance::query()->create([
            'id' => $id,
        ]);
    }

    public function saveCredentials(string $instanceId, string $passwordHash, string $recoveryHash): void
    {
        Instance::query()->findOrFail($instanceId)->forceFill([
            'owner_password_hash' => $passwordHash,
            'recovery_code_hash' => $recoveryHash,
            'app_key_check' => 'telebezel-app-key-v1:'.$instanceId,
        ])->save();
    }

    public function consumeBootstrap(string $id): void
    {
        DB::table('bootstrap_codes')->where('id', $id)->update([
            'consumed_at' => now(),
            'updated_at' => now(),
        ]);
    }

    public function saveRecoveryHash(string $instanceId, string $hash): void
    {
        Instance::query()->findOrFail($instanceId)->forceFill([
            'recovery_code_hash' => $hash,
        ])->save();
    }
}
