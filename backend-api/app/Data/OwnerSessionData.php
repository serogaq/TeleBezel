<?php

declare(strict_types=1);

namespace App\Data;

use Carbon\CarbonImmutable;

final readonly class OwnerSessionData
{
    public function __construct(public string $id, public string $instanceId, public CarbonImmutable $authenticatedAt, public CarbonImmutable $lastInteractiveAt, public CarbonImmutable $expiresAt) {}
}
