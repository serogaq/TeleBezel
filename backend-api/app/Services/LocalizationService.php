<?php

declare(strict_types=1);

namespace App\Services;

// Chooses the language of the settings page and loads its strings. The choice
// is the viewer's cookie, then the browser's preference, then English.
final class LocalizationService
{
    public const string COOKIE = 'telebezel_locale';

    public const array LOCALES = ['en', 'ru'];

    public function locale(mixed $chosen, ?string $preferred): string
    {
        if (is_string($chosen) && in_array($chosen, self::LOCALES, true)) {
            return $chosen;
        }

        return $preferred === 'ru' ? 'ru' : 'en';
    }

    /** @return array<string, string> */
    public function strings(string $locale): array
    {
        $locale = in_array($locale, self::LOCALES, true) ? $locale : 'en';
        $strings = $this->load('en');

        return $locale === 'en' ? $strings : array_replace($strings, $this->load($locale));
    }

    /** @return array<string, string> */
    private function load(string $locale): array
    {
        $decoded = json_decode((string) file_get_contents(base_path("localization/{$locale}/backend.json")), true);
        $strings = [];
        foreach (is_array($decoded) ? $decoded : [] as $key => $value) {
            if (is_string($key) && is_string($value)) {
                $strings[$key] = $value;
            }
        }

        return $strings;
    }
}
