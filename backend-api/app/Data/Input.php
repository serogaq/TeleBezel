<?php

declare(strict_types=1);

namespace App\Data;

use App\Support\Values;

final readonly class Input
{
    /** @param array<string, mixed> $values */
    public function __construct(private array $values) {}

    public function has(string $key): bool
    {
        return array_key_exists($key, $this->values);
    }

    public function string(string $key): string
    {
        return Values::string($this->values[$key] ?? null);
    }

    public function nullableString(string $key): ?string
    {
        return ($this->values[$key] ?? null) === null ? null : $this->string($key);
    }

    public function integer(string $key): int
    {
        return Values::integer($this->values[$key] ?? null);
    }

    /** @return list<string> */
    public function strings(string $key): array
    {
        return Values::strings($this->values[$key] ?? null);
    }

    /** @return array<string, mixed> */
    public function all(): array
    {
        return $this->values;
    }
}
