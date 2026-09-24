<?php

declare(strict_types=1);

namespace App\Repositories;

use App\Contracts\Repositories\AccessTokenRepository as AccessTokenRepositoryContract;
use App\Data\AccessTokenData;
use App\Data\TokenClaims;
use App\Enums\TokenType;
use App\Models\AccessToken;
use App\Models\DeviceProfile;
use App\Models\Instance;
use Carbon\CarbonImmutable;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Str;

final class AccessTokenRepository implements AccessTokenRepositoryContract
{
    public function find(string $hash): ?AccessTokenData
    {
        $token = AccessToken::query()->where('token_hash', $hash)->whereNull('revoked_at')->first();
        if ($token === null) {
            return null;
        }

        return new AccessTokenData($token->id, $token->instance_id, $token->type, TokenClaims::parse($token->type, $token->claims), $token->expires_at, $token->idle_timeout_seconds, $token->last_active_at, $token->last_used_at);
    }

    public function touch(AccessTokenData $token): void
    {
        if ($token->lastUsedAt !== null && $token->lastUsedAt->isAfter(now()->subMinute())) {
            return;
        }
        AccessToken::query()->whereKey($token->id)->update([
            'last_used_at' => now(),
        ]);
    }

    public function issue(string $instanceId, TokenType $type, string $name, string $hash, string $prefix, TokenClaims $claims, ?CarbonImmutable $expiresAt = null, ?int $idleTimeoutSeconds = null): string
    {
        return DB::transaction(function () use ($instanceId, $type, $name, $hash, $prefix, $claims, $expiresAt, $idleTimeoutSeconds): string {
            $id = (string) Str::uuid();
            AccessToken::query()->create([
                'id' => $id,
                'instance_id' => $instanceId,
                'name' => $name,
                'type' => $type,
                'token_prefix' => $prefix,
                'token_hash' => $hash,
                'claims' => $claims->toArray(),
                'expires_at' => $expiresAt,
                'idle_timeout_seconds' => $idleTimeoutSeconds,
                'last_active_at' => $idleTimeoutSeconds === null ? null : now(),
            ]);
            if ($type === TokenType::Device) {
                DeviceProfile::query()->create([
                    'token_id' => $id,
                ]);
            }

            return $id;
        }, 3);
    }

    public function lockInstance(string $instanceId): void
    {
        Instance::query()->whereKey($instanceId)->lockForUpdate()->firstOrFail();
    }

    public function activeCount(string $instanceId, TokenType $type): int
    {
        return AccessToken::query()->where('instance_id', $instanceId)->where('type', $type->value)->whereNull('revoked_at')->where(function ($query): void {
            $query->whereNull('expires_at')->orWhere('expires_at', '>', now());
        })->count();
    }

    /** @return array<int, array<string, mixed>> */
    public function listing(string $instanceId, TokenType $type): array
    {
        return AccessToken::query()->where('instance_id', $instanceId)->where('type', $type->value)->orderBy('created_at')->get()->map(function (AccessToken $token): array {
            $profile = DeviceProfile::query()->find($token->id);

            return [
                'id' => $token->id,
                'name' => $token->name,
                'type' => $token->type->value,
                'permissions' => TokenClaims::parse($token->type, $token->claims)->permissions ?? [],
                'locale' => $profile?->getAttribute('locale'),
                'chat_list' => $profile?->getAttribute('chat_list'),
                'last_seen_at' => $token->last_used_at?->toIso8601String(),
                'revoked_at' => $token->revoked_at?->toIso8601String(),
            ];
        })->values()->all();
    }

    public function revoke(string $id, ?string $instanceId = null, ?TokenType $type = null): ?TokenType
    {
        $query = AccessToken::query()->whereKey($id);
        if ($instanceId !== null) {
            $query->where('instance_id', $instanceId);
        }
        if ($type !== null) {
            $query->where('type', $type->value);
        }
        $token = $query->first();
        if ($token === null) {
            return null;
        }
        if ($token->revoked_at === null) {
            $token->forceFill([
                'revoked_at' => now(),
            ])->save();
        }

        return $token->type;
    }

    public function revokeInstance(string $instanceId): void
    {
        AccessToken::query()->where('instance_id', $instanceId)->whereNull('revoked_at')->update([
            'revoked_at' => now(),
        ]);
    }

    public function activity(string $id): void
    {
        AccessToken::query()->whereKey($id)->whereNull('revoked_at')->update([
            'last_active_at' => now(),
        ]);
    }
}
