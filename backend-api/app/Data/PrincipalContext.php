<?php

declare(strict_types=1);

namespace App\Data;

final readonly class PrincipalContext
{
    public function __construct(public string $type, public string $id, public ?string $instanceId = null) {}
}
