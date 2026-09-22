<?php

declare(strict_types=1);

namespace App\Contracts\Repositories;

interface SettingsRepository
{
    /** @return array<string, mixed> */
    public function show(string $instanceId): array;

    /** @param array<string, mixed> $data
     * @return array<int, string> */
    public function update(string $instanceId, array $data): array;
}
