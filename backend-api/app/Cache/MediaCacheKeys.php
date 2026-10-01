<?php

declare(strict_types=1);

namespace App\Cache;

final class MediaCacheKeys
{
    // A rendition is tied to the session it was read in, the exact Telegram
    // file and the watch spec, so a changed attachment, another session or a
    // new pipeline never finds an older image.
    public static function rendition(string $accountId, string $storageGeneration, string $authorizationGeneration, string $runtimeEpoch, string $messageId, string $uniqueId, string $spec, string $pipeline): string
    {
        return hash('sha256', implode("\x1f", [$accountId, $storageGeneration, $authorizationGeneration, $runtimeEpoch, $messageId, $uniqueId, $spec, $pipeline]));
    }
}
