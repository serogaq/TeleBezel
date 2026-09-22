<?php

declare(strict_types=1);

namespace App\Repositories;

use App\Contracts\Repositories\HealthRepository as HealthRepositoryContract;
use Illuminate\Support\Facades\DB;

final class HealthRepository implements HealthRepositoryContract
{
    public function check(): void
    {
        DB::selectOne('select 1 as ready');
    }
}
