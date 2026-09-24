<?php

declare(strict_types=1);

namespace App\Data;

use App\Enums\TokenType;
use Carbon\CarbonImmutable;

final readonly class AccessTokenData
{
    public function __construct(
        public string $id,
        public string $instanceId,
        public TokenType $type,
        public ?TokenClaims $claims,
        public ?CarbonImmutable $expiresAt,
        public ?int $idleTimeoutSeconds,
        public ?CarbonImmutable $lastActiveAt,
        public ?CarbonImmutable $lastUsedAt,
    ) {}
}
