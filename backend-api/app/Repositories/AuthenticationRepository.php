<?php

declare(strict_types=1);

namespace App\Repositories;

use App\Contracts\Repositories\AuthenticationRepository as AuthenticationRepositoryContract;
use App\Data\OwnerSessionData;
use App\Data\PrincipalContext;
use App\Models\ApiClient;
use App\Models\Device;
use App\Models\OwnerSession;

final class AuthenticationRepository implements AuthenticationRepositoryContract
{
    public function token(string $hash): ?PrincipalContext
    {
        $client = ApiClient::query()->where('token_hash', $hash)->whereNull('revoked_at')->first();
        if ($client !== null) {
            return new PrincipalContext('api_client', $client->id);
        }
        $device = Device::query()->where('token_hash', $hash)->whereNull('revoked_at')->first();
        if ($device === null) {
            return null;
        }
        $device->forceFill([
            'last_seen_at' => now(),
        ])->save();

        return new PrincipalContext('device', $device->id, $device->instance_id);
    }

    public function owner(string $hash): ?OwnerSessionData
    {
        $session = OwnerSession::query()->where('token_hash', $hash)->whereNull('revoked_at')->first();
        if ($session === null) {
            return null;
        }

        return new OwnerSessionData($session->id, $session->instance_id, $session->authenticated_at, $session->last_interactive_at, $session->expires_at);
    }

    public function revokeOwnerSession(string $id): void
    {
        OwnerSession::query()->whereKey($id)->update([
            'revoked_at' => now(),
        ]);
    }
}
