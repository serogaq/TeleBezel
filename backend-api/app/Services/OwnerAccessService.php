<?php

namespace App\Services;

use App\Exceptions\ApiException;
use App\Models\Device;
use App\Models\Instance;
use App\Models\OwnerSession;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Hash;
use Illuminate\Support\Str;

final class OwnerAccessService
{
    /** @return array{Instance, string, string} */
    public function bootstrap(string $code, string $password): array
    {
        return DB::transaction(function () use ($code, $password): array {
            $entry = DB::table('bootstrap_codes')->where('code_hash', hash('sha256', $code))->lockForUpdate()->first();
            if ($entry === null || $entry->consumed_at !== null || now()->greaterThan($entry->expires_at)) {
                throw new ApiException('bootstrap.invalid', 422);
            }
            $instance = Instance::query()->lockForUpdate()->first();
            if ($instance?->owner_password_hash !== null) {
                throw new ApiException('bootstrap.consumed', 409);
            }
            $instance ??= Instance::query()->create(['id' => (string) Str::uuid()]);
            [$recovery, $recoveryHash] = $this->newRecoveryCode();
            $instance->forceFill(['owner_password_hash' => Hash::make($password), 'recovery_code_hash' => $recoveryHash,
                'app_key_check' => 'telebezel-app-key-v1:'.$instance->id])->save();
            DB::table('bootstrap_codes')->where('id', $entry->id)->update(['consumed_at' => now(), 'updated_at' => now()]);
            [$session, $token] = $this->newSession($instance);

            return [$instance, $token, $recovery];
        }, 3);
    }

    /** @return array{Instance, string} */
    public function login(string $password): array
    {
        $instance = Instance::query()->first();
        if ($instance === null || $instance->owner_password_hash === null || ! Hash::check($password, $instance->owner_password_hash)) {
            throw new ApiException('owner.invalid_credentials', 422);
        }
        [, $token] = $this->newSession($instance);

        return [$instance, $token];
    }

    /** @return array{string, string} */
    public function recover(string $recoveryCode, string $password): array
    {
        return DB::transaction(function () use ($recoveryCode, $password): array {
            $instance = Instance::query()->lockForUpdate()->first();
            if ($instance === null || $instance->recovery_code_hash === null || ! Hash::check($recoveryCode, $instance->recovery_code_hash)) {
                throw new ApiException('owner.invalid_recovery_code', 422);
            }
            [$newRecovery, $newRecoveryHash] = $this->newRecoveryCode();
            $instance->forceFill(['owner_password_hash' => Hash::make($password), 'recovery_code_hash' => $newRecoveryHash])->save();
            OwnerSession::query()->whereNull('revoked_at')->update(['revoked_at' => now()]);
            Device::query()->whereNull('revoked_at')->update(['revoked_at' => now()]);
            [, $token] = $this->newSession($instance);

            return [$token, $newRecovery];
        }, 3);
    }

    public function rotateRecoveryCode(string $instanceId): string
    {
        return DB::transaction(function () use ($instanceId): string {
            $instance = Instance::query()->whereKey($instanceId)->lockForUpdate()->firstOrFail();
            [$recovery, $recoveryHash] = $this->newRecoveryCode();
            $instance->forceFill(['recovery_code_hash' => $recoveryHash])->save();

            return $recovery;
        }, 3);
    }

    /** @return array{OwnerSession, string} */
    private function newSession(Instance $instance): array
    {
        $token = 'tbo_'.Str::random(48);
        $session = OwnerSession::query()->create(['id' => (string) Str::uuid(), 'instance_id' => $instance->id,
            'token_hash' => hash('sha256', $token), 'authenticated_at' => now(), 'last_interactive_at' => now(), 'expires_at' => now()->addHours(12)]);

        return [$session, $token];
    }

    /** @return array{string, string} */
    private function newRecoveryCode(): array
    {
        $value = implode('-', str_split(strtoupper(Str::random(24)), 6));

        return [$value, Hash::make($value)];
    }
}
