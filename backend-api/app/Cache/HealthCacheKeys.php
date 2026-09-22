<?php

declare(strict_types=1);

namespace App\Cache;

final class HealthCacheKeys
{
    public static function readiness(): string
    {
        return 'telebezel:health:readiness';
    }
}
