<?php

declare(strict_types=1);

namespace App\Support;

use App\Exceptions\ApiException;

final class Values
{
    public static function string(mixed $value): string
    {
        if (! is_string($value)) {
            throw new ApiException('request.invalid', 422);
        }

        return $value;
    }

    public static function integer(mixed $value): int
    {
        if (is_int($value)) {
            return $value;
        }
        if (is_string($value) && preg_match('/^-?\d+$/D', $value) === 1) {
            $integer = filter_var($value, FILTER_VALIDATE_INT);
            if (is_int($integer)) {
                return $integer;
            }
        }
        throw new ApiException('request.invalid', 422);
    }

    /** @return array<string, mixed> */
    public static function object(mixed $value): array
    {
        if (! is_array($value)) {
            throw new ApiException('request.invalid', 422);
        }
        $result = [];
        foreach ($value as $key => $item) {
            if (! is_string($key)) {
                throw new ApiException('request.invalid', 422);
            }
            $result[$key] = $item;
        }

        return $result;
    }

    /** @return list<string> */
    public static function strings(mixed $value): array
    {
        if (! is_array($value) || ! array_is_list($value)) {
            throw new ApiException('request.invalid', 422);
        }

        return array_map(self::string(...), $value);
    }
}
