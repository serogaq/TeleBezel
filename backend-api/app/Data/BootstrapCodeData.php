<?php

declare(strict_types=1);

namespace App\Data;

use Carbon\CarbonImmutable;

final readonly class BootstrapCodeData
{
    public function __construct(public string $id, public CarbonImmutable $expiresAt, public bool $consumed) {}
}
