<?php

declare(strict_types=1);

namespace App\Repositories;

use App\Contracts\Repositories\RetentionRepository as RetentionRepositoryContract;
use Carbon\CarbonImmutable;
use Illuminate\Database\Query\Builder;
use Illuminate\Support\Facades\DB;

final class RetentionRepository implements RetentionRepositoryContract
{
    public const IDEMPOTENCY_DAYS = 7;

    public const SESSION_DAYS = 7;

    /** @return array<string, int> */
    public function purge(CarbonImmutable $now): array
    {
        $idempotency = $now->subDays(self::IDEMPOTENCY_DAYS);
        $sessions = $now->subDays(self::SESSION_DAYS);

        return DB::transaction(function () use ($now, $idempotency, $sessions): array {
            $counts = [
                'account_idempotency_keys' => DB::table('account_idempotency_keys')->where('created_at', '<', $idempotency)->delete(),
                'instance_account_idempotency_keys' => DB::table('instance_account_idempotency_keys')->where('created_at', '<', $idempotency)->delete(),
                'owner_account_idempotency_keys' => DB::table('owner_account_idempotency_keys')->where('created_at', '<', $idempotency)->delete(),
            ];
            $expired = DB::table('owner_sessions')->where(function (Builder $query) use ($sessions): void {
                $query->where('expires_at', '<', $sessions)->orWhere('revoked_at', '<', $sessions);
            })->whereNotExists(function (Builder $query): void {
                $query->select(DB::raw(1))->from('owner_account_idempotency_keys')->whereColumn('owner_account_idempotency_keys.owner_session_id', 'owner_sessions.id');
            });
            $counts['owner_sessions'] = $expired->delete();
            $counts['bootstrap_codes'] = DB::table('bootstrap_codes')->where(function (Builder $query) use ($now): void {
                $query->where('expires_at', '<', $now->subDay())->orWhere('consumed_at', '<', $now->subDay());
            })->delete();

            return $counts;
        }, 3);
    }
}
