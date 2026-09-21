<?php

declare(strict_types=1);

namespace App\Contracts\Repositories;

use App\Data\Input;

interface DeviceRepository
{
    /** @return array<string, mixed> */
    public function preferences(string $id): array;

    /** @return array<int, array<string, mixed>> */
    public function all(string $instanceId): array;

    public function update(string $id, Input $input): void;

    /** @return array{id: string, name: string} */
    public function create(string $instanceId, string $name, string $tokenHash, string $prefix): array;

    public function revoke(string $instanceId, string $id): void;
}
