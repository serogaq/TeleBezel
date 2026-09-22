<?php

declare(strict_types=1);

namespace App\Cache;

final class ReconciliationCacheKeys
{
    public static function lock(): string
    {
        return 'telebezel:accounts-reconcile';
    }

    public static function cursor(int $phase): string
    {
        return "telebezel:accounts-reconcile:cursor:{$phase}";
    }
}
