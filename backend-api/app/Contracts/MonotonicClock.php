<?php

declare(strict_types=1);

namespace App\Contracts;

interface MonotonicClock
{
    public function nanoseconds(): int;
}
