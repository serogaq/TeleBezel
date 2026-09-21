<?php

declare(strict_types=1);

namespace App\Contracts\Repositories;

use App\Data\OwnerSessionData;
use App\Data\PrincipalContext;

interface AuthenticationRepository
{
    public function token(string $hash): ?PrincipalContext;

    public function owner(string $hash): ?OwnerSessionData;

    public function revokeOwnerSession(string $id): void;
}
