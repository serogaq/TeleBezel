<?php

declare(strict_types=1);

namespace App\Contracts\Repositories;

use Carbon\CarbonImmutable;

interface RetentionRepository
{
    /** @return array<string, int> */
    public function purge(CarbonImmutable $now): array;
}
