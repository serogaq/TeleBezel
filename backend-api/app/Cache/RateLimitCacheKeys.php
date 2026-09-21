<?php

declare(strict_types=1);

namespace App\Cache;

final class RateLimitCacheKeys
{
    public static function publicApi(string $principalId): string
    {
        return 'public-api:'.$principalId;
    }

    public static function accountOperation(string $budget, string $subject): string
    {
        return "account-operation:{$budget}:{$subject}";
    }
}
