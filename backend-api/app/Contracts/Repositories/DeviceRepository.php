<?php

declare(strict_types=1);

namespace App\Contracts\Repositories;

use App\Data\Input;

interface DeviceRepository
{
    /** @return array<string, mixed> */
    public function preferences(string $tokenId): array;

    public function update(string $tokenId, Input $input): void;
}
