<?php

declare(strict_types=1);

namespace Tests\Fakes;

use App\Contracts\MonotonicClock;
use InvalidArgumentException;

final class FakeMonotonicClock implements MonotonicClock
{
    private int $time = 0;

    public function nanoseconds(): int
    {
        return $this->time;
    }

    public function advanceSeconds(int $seconds): void
    {
        if ($seconds < 0) {
            throw new InvalidArgumentException('A monotonic clock cannot move backwards.');
        }
        $this->time += $seconds * 1_000_000_000;
    }
}
