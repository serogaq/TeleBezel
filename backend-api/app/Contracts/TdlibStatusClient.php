<?php

declare(strict_types=1);

namespace App\Contracts;

interface TdlibStatusClient
{
    /** @return array{status: string, tdlib_version: string, accounts: int} */
    public function status(): array;
}
