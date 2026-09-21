<?php

namespace App\Console\Commands;

use App\Enums\AccountLifecycle;
use App\Exceptions\ApiException;
use App\Models\TelegramAccount;
use App\Services\TdlibGateway;
use App\Services\TelegramAccountService;
use Illuminate\Console\Command;
use Illuminate\Support\Facades\Cache;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Str;

final class ReconcileAccounts extends Command
{
    protected $signature = 'telebezel:accounts-reconcile {--dry-run : Report desired accounts without activating clients}';

    protected $description = 'Reconcile durable Telegram account intent with the TDLib runtime';

    public function handle(TelegramAccountService $accounts, TdlibGateway $tdlib): int
    {
        $lock = Cache::lock('telebezel:accounts-reconcile', 120);
        if (! $lock->get()) {
            $this->warn('Another reconciliation pass is active.');

            return self::SUCCESS;
        }
        $started = hrtime(true);
        $requestId = (string) Str::uuid();
        $succeeded = 0;
        $failed = 0;
        $deferred = 0;
        $budgetExhausted = false;
        DB::table('scheduler_statuses')->upsert([['name' => 'reconciliation', 'last_tick_at' => now(),
            'run_started_at' => now(), 'run_finished_at' => null, 'last_result' => 'running', 'duration_ms' => null,
            'succeeded' => 0, 'failed' => 0, 'deferred' => 0, 'created_at' => now(), 'updated_at' => now()]], ['name']);
        try {
            $query = TelegramAccount::withTrashed()
                ->where('lifecycle', '!=', AccountLifecycle::Removed->value);
            if ($this->option('dry-run')) {
                $local = (clone $query)->orderBy('id')->get(['id', 'storage_generation', 'lifecycle', 'desired_revision']);
                $remote = $tdlib->listSnapshots([], $requestId);
                $this->line(json_encode(['desired' => $local->toArray(), 'runtime' => $remote], JSON_THROW_ON_ERROR | JSON_UNESCAPED_SLASHES));

                return self::SUCCESS;
            }
            $phases = [
                [AccountLifecycle::LogoutPending->value, AccountLifecycle::Removing->value, AccountLifecycle::Provisioning->value],
                [AccountLifecycle::Active->value],
            ];
            foreach ($phases as $phase => $lifecycles) {
                $cursorKey = "telebezel:accounts-reconcile:cursor:{$phase}";
                $cursor = Cache::get($cursorKey);
                $cursor = is_string($cursor) ? $cursor : null;
                $process = function ($batch) use ($accounts, $requestId, $started, $cursorKey, &$succeeded, &$failed, &$deferred, &$budgetExhausted): bool {
                    foreach ($batch as $account) {
                        if ((hrtime(true) - $started) / 1_000_000_000 >= 35) {
                            $budgetExhausted = true;

                            return false;
                        }
                        Cache::forever($cursorKey, $account->id);
                        if (($account->reconcile_blocked_revision !== null && $account->reconcile_blocked_revision === $account->desired_revision)
                            || ($account->next_reconcile_at !== null && $account->next_reconcile_at->isFuture())) {
                            $deferred++;

                            continue;
                        }
                        $account->forceFill(['last_reconcile_attempt_at' => now()])->save();
                        try {
                            $accounts->reconcile($account, $requestId);
                            TelegramAccount::query()->whereKey($account->id)->where('desired_revision', $account->desired_revision)->update([
                                'last_reconcile_success_at' => now(), 'next_reconcile_at' => null, 'reconcile_failures' => 0,
                                'reconcile_blocked_revision' => null,
                            ]);
                            $succeeded++;
                        } catch (ApiException $exception) {
                            $permanent = in_array($exception->errorCode, ['storage.invalid_key', 'configuration.invalid',
                                'configuration.missing', 'configuration.environment_mismatch'], true);
                            $failures = min(10, ((int) $account->reconcile_failures) + 1);
                            $base = min(900, 60 * (2 ** min(4, $failures - 1)));
                            $delay = max($exception->retryAfter ?? 0, $base + random_int(0, min(30, intdiv($base, 4))));
                            TelegramAccount::query()->whereKey($account->id)->where('desired_revision', $account->desired_revision)->update([
                                'reconcile_failures' => $failures,
                                'next_reconcile_at' => $permanent ? null : now()->addSeconds($delay),
                                'reconcile_blocked_revision' => $permanent ? $account->desired_revision : null,
                                'last_error_code' => $exception->errorCode,
                            ]);
                            $failed++;
                            $this->warn("{$account->id}: {$exception->errorCode}");
                        }
                    }

                    return true;
                };
                $after = (clone $query)->whereIn('lifecycle', $lifecycles)->orderBy('id');
                if ($cursor !== null) {
                    $after->where('id', '>', $cursor);
                }
                $continue = $after->chunkById(25, $process, 'id');
                if ($continue !== false && $cursor !== null && (hrtime(true) - $started) / 1_000_000_000 < 45) {
                    $continue = (clone $query)->whereIn('lifecycle', $lifecycles)->where('id', '<=', $cursor)
                        ->orderBy('id')->chunkById(25, $process, 'id');
                }
                if ($continue === false || (hrtime(true) - $started) / 1_000_000_000 >= 45) {
                    break;
                }
            }
        } finally {
            $duration = (int) ((hrtime(true) - $started) / 1_000_000);
            DB::table('scheduler_statuses')->where('name', 'reconciliation')->update([
                'run_finished_at' => now(), 'last_result' => $budgetExhausted ? 'budget_exhausted' : 'completed',
                'duration_ms' => $duration, 'succeeded' => $succeeded, 'failed' => $failed, 'deferred' => $deferred,
                'updated_at' => now(),
            ]);
            $lock->release();
        }

        return self::SUCCESS;
    }
}
