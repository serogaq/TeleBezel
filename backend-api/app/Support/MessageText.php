<?php

declare(strict_types=1);

namespace App\Support;

use Normalizer;

final class MessageText
{
    public const int MAXIMUM_UTF16_UNITS = 4096;

    public static function canonical(string $text): ?string
    {
        if (! mb_check_encoding($text, 'UTF-8')) {
            return null;
        }
        $normalized = Normalizer::normalize(str_replace(["\r\n", "\r"], "\n", $text), Normalizer::FORM_C);
        if (! is_string($normalized)) {
            return null;
        }
        $stripped = preg_replace('/[^\P{Cc}\n]/u', '', $normalized);

        return is_string($stripped) ? trim($stripped) : null;
    }

    public static function utf16Length(string $text): int
    {
        return intdiv(strlen(mb_convert_encoding($text, 'UTF-16LE', 'UTF-8')), 2);
    }
}
