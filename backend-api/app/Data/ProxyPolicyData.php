<?php

declare(strict_types=1);

namespace App\Data;

use Carbon\CarbonImmutable;

final readonly class ProxyPolicyData
{
    public function __construct(public string $instanceId, public ?string $activeId, public string $failureAction, public int $timeoutSeconds, public ?CarbonImmutable $failureStartedAt) {}
}
