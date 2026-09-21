<?php

declare(strict_types=1);

namespace App\Repositories;

use App\Contracts\Repositories\SchedulerRepository as SchedulerRepositoryContract;
use Illuminate\Support\Facades\DB;

final class SchedulerRepository implements SchedulerRepositoryContract
{
    public function started(): void
    {
        DB::table('scheduler_statuses')->upsert([[
            'name' => 'reconciliation',
            'last_tick_at' => now(),
            'run_started_at' => now(),
            'run_finished_at' => null,
            'last_result' => 'running',
            'duration_ms' => null,
            'succeeded' => 0,
            'failed' => 0,
            'deferred' => 0,
            'created_at' => now(),
            'updated_at' => now(),
        ]], ['name']);
    }

    public function finished(int $duration, int $succeeded, int $failed, int $deferred, bool $budgetExhausted): void
    {
        DB::table('scheduler_statuses')->where('name', 'reconciliation')->update([
            'run_finished_at' => now(),
            'last_result' => $budgetExhausted ? 'budget_exhausted' : 'completed',
            'duration_ms' => $duration,
            'succeeded' => $succeeded,
            'failed' => $failed,
            'deferred' => $deferred,
            'updated_at' => now(),
        ]);
    }
}
