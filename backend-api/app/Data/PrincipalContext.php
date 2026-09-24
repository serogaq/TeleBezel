<?php

declare(strict_types=1);

namespace App\Data;

use App\Enums\Permission;

final readonly class PrincipalContext
{
    /** @param list<string> $permissions
     * @param '*'|list<string> $accounts */
    public function __construct(public string $type, public string $id, public ?string $instanceId = null, public array $permissions = [], public string|array $accounts = '*') {}

    public function can(Permission|string $permission): bool
    {
        return in_array($permission instanceof Permission ? $permission->value : $permission, $this->permissions, true);
    }

    public function canAccessAccount(string $uuid): bool
    {
        return $this->accounts === '*' || in_array(strtolower($uuid), $this->accounts, true);
    }

    /** @return list<string>|null */
    public function accountFilter(): ?array
    {
        return $this->accounts === '*' ? null : $this->accounts;
    }
}
