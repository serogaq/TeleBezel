<?php

declare(strict_types=1);

namespace App\Data;

use App\Enums\TokenType;
use App\Exceptions\ApiException;
use Illuminate\Support\Str;

final readonly class TokenClaims
{
    /** @param list<string> $permissions
     * @param '*'|list<string> $accounts */
    private function __construct(public array $permissions, public string|array $accounts) {}

    public static function full(TokenType $type): self
    {
        return new self($type->permissionNames(), '*');
    }

    /** @param list<string> $permissions
     * @param '*'|list<string> $accounts */
    public static function issue(TokenType $type, array $permissions, string|array $accounts = '*'): self
    {
        return self::parse($type, [
            'permissions' => $permissions,
            'accounts' => $accounts,
        ]) ?? throw new ApiException('request.invalid', 422);
    }

    public static function parse(TokenType $type, mixed $claims): ?self
    {
        if (! is_array($claims) || ! is_array($claims['permissions'] ?? null) || ! array_key_exists('accounts', $claims)) {
            return null;
        }
        $permissions = [];
        foreach ($claims['permissions'] as $permission) {
            if (! is_string($permission) || ! $type->allows($permission)) {
                return null;
            }
            $permissions[$permission] = $permission;
        }
        $accounts = $claims['accounts'];
        if ($accounts !== '*') {
            if (! is_array($accounts) || ! array_is_list($accounts)) {
                return null;
            }
            $ids = [];
            foreach ($accounts as $account) {
                if (! is_string($account) || ! Str::isUuid($account)) {
                    return null;
                }
                $ids[strtolower($account)] = strtolower($account);
            }
            $accounts = array_values($ids);
        }

        return new self(array_values($permissions), $accounts);
    }

    /** @return array{permissions: list<string>, accounts: '*'|list<string>} */
    public function toArray(): array
    {
        return [
            'permissions' => $this->permissions,
            'accounts' => $this->accounts,
        ];
    }
}
