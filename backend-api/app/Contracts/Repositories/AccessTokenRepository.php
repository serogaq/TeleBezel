<?php

declare(strict_types=1);

namespace App\Contracts\Repositories;

use App\Data\AccessTokenData;
use App\Data\TokenClaims;
use App\Enums\TokenType;
use Carbon\CarbonImmutable;

interface AccessTokenRepository
{
    public function find(string $hash): ?AccessTokenData;

    public function touch(AccessTokenData $token): void;

    public function issue(string $instanceId, TokenType $type, string $name, string $hash, string $prefix, TokenClaims $claims, ?CarbonImmutable $expiresAt = null, ?int $idleTimeoutSeconds = null): string;

    public function lockInstance(string $instanceId): void;

    public function activeCount(string $instanceId, TokenType $type): int;

    /** @return array<int, array<string, mixed>> */
    public function listing(string $instanceId, TokenType $type): array;

    public function revoke(string $id, ?string $instanceId = null, ?TokenType $type = null): ?TokenType;

    public function revokeInstance(string $instanceId): void;

    public function activity(string $id): void;
}
