<?php

declare(strict_types=1);

namespace App\Contracts\Repositories;

interface AdministrationRepository
{
    public function bootstrap(string $hash): void;
}
