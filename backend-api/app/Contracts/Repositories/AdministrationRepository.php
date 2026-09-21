<?php

declare(strict_types=1);

namespace App\Contracts\Repositories;

interface AdministrationRepository
{
    public function issue(string $name, string $hash, string $prefix): string;

    public function revoke(string $id): bool;

    public function bootstrap(string $hash): void;
}
