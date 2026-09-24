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
                'message_sends' => DB::table('message_sends')->where('created_at', '<', $idempotency)->delete(),
            ];
            $counts['access_tokens'] = DB::table('access_tokens')->whereNotNull('expires_at')->where(function (Builder $query) use ($sessions): void {
                $query->where('expires_at', '<', $sessions)->orWhere('revoked_at', '<', $sessions);
            })->delete();
            $counts['bootstrap_codes'] = DB::table('bootstrap_codes')->where(function (Builder $query) use ($now): void {
                $query->where('expires_at', '<', $now->subDay())->orWhere('consumed_at', '<', $now->subDay());
            })->delete();

            return $counts;
        }, 3);
    }
}
