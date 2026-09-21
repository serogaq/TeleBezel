<?php

declare(strict_types=1);

namespace App\Infrastructure\Time;

use App\Contracts\MonotonicClock;

final class SystemMonotonicClock implements MonotonicClock
{
    public function nanoseconds(): int
    {
        return hrtime(true);
    }
}
