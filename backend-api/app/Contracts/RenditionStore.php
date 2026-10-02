<?php

declare(strict_types=1);

namespace App\Contracts;

interface RenditionStore
{
    public function get(string $accountId, string $key): ?string;

    public function put(string $accountId, string $key, string $rendition): void;

    public function forgetAccount(string $accountId): void;
}
