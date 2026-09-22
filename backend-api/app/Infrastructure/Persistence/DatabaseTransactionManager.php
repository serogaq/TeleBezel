<?php

declare(strict_types=1);

namespace App\Infrastructure\Persistence;

use App\Contracts\TransactionManager;
use Closure;
use Illuminate\Support\Facades\DB;

final class DatabaseTransactionManager implements TransactionManager
{
    public function run(Closure $operation): mixed
    {
        return DB::transaction($operation, 3);
    }
}
