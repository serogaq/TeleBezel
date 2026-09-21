<?php

declare(strict_types=1);

namespace App\Data;

final readonly class OwnerCredentials
{
    public function __construct(public string $id, public ?string $passwordHash, public ?string $recoveryHash, public ?string $appKeyCheck = null) {}
}
