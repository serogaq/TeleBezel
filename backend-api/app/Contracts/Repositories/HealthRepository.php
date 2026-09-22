<?php

declare(strict_types=1);

namespace App\Contracts\Repositories;

interface HealthRepository
{
    public function check(): void;
}
