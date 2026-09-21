<?php

declare(strict_types=1);

namespace App\Cache;

final class ProxyCacheKeys
{
    public static function monitorLock(): string
    {
        return 'telebezel:proxy-monitor';
    }
}
