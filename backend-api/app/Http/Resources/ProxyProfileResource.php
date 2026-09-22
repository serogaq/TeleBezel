<?php

declare(strict_types=1);

namespace App\Http\Resources;

final class ProxyProfileResource extends ApiResource
{
    private const array FIELDS = ['id', 'label', 'mode', 'host', 'port', 'http_only', 'username', 'has_credentials', 'position', 'active', 'ping'];

    /** @param array<string, mixed> $data */
    public function __construct(array $data)
    {
        parent::__construct(self::project($data));
    }

    /** @param array<string, mixed> $data
     * @return array<string, mixed> */
    private static function project(array $data): array
    {
        return array_intersect_key($data, array_flip(self::FIELDS));
    }

    /** @param array<int, array<string, mixed>> $items */
    public static function listing(array $items): ApiResource
    {
        return new ApiResource(array_values(array_map(self::project(...), $items)));
    }
}
