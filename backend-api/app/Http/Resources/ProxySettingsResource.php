<?php

declare(strict_types=1);

namespace App\Http\Resources;

final class ProxySettingsResource extends ApiResource
{
    private const array FIELDS = ['failure_action', 'connect_timeout_seconds'];

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
}
