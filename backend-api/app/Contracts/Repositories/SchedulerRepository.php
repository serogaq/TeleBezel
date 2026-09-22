<?php

declare(strict_types=1);

namespace App\Contracts\Repositories;

interface SchedulerRepository
{
    public function started(): void;

    public function finished(int $duration, int $succeeded, int $failed, int $deferred, bool $budgetExhausted): void;
}
