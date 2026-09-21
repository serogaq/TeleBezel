<?php

declare(strict_types=1);

namespace App\Services;

use App\Cache\ReconciliationCacheKeys;
use App\Contracts\MonotonicClock;
use App\Contracts\Repositories\SchedulerRepository;
use App\Contracts\Repositories\TelegramAccountRepository;
use App\Contracts\TdlibGateway;
use App\Exceptions\ApiException;
use Illuminate\Support\Facades\Cache;
use Illuminate\Support\Str;

final readonly class ReconciliationService
{
    public function __construct(private TelegramAccountRepository $repository, private SchedulerRepository $scheduler, private TelegramAccountService $accounts, private TdlibGateway $tdlib, private MonotonicClock $clock) {}

    /** @return array<int, string> */
    public function run(bool $dryRun): array
    {
        $requestId = (string) Str::uuid();
        if ($dryRun) {
            return [json_encode([
                'desired' => $this->repository->desired(),
                'runtime' => $this->tdlib->listSnapshots([], $requestId),
            ], JSON_THROW_ON_ERROR | JSON_UNESCAPED_SLASHES)];
        }
        $lock = Cache::lock(ReconciliationCacheKeys::lock(), 120);
        if ($lock->get() !== true) {
            return ['Another reconciliation pass is active.'];
        }
        $started = $this->clock->nanoseconds();
        $succeeded = 0;
        $failed = 0;
        $deferred = 0;
        $budgetExhausted = false;
        $messages = [];
        try {
            $this->scheduler->started();
            foreach ([['logout_pending', 'removing', 'provisioning'], ['active']] as $phase => $lifecycles) {
                $key = ReconciliationCacheKeys::cursor($phase);
                $saved = Cache::get($key);
                $saved = is_string($saved) ? $saved : null;
                foreach ($saved === null ? [[null, null]] : [[$saved, null], [null, $saved]] as [$after, $through]) {
                    do {
                        $batch = $this->repository->batch($lifecycles, $after, $through);
                        foreach ($batch as $account) {
                            if (($this->clock->nanoseconds() - $started) / 1000000000 >= 35) {
                                $budgetExhausted = true;
                                break 4;
                            }
                            $after = $account->id;
                            Cache::forever($key, $after);
                            if ($account->reconcile_blocked_revision === $account->desired_revision || $account->next_reconcile_at?->isFuture() === true) {
                                $deferred++;

                                continue;
                            }
                            $this->repository->attempted($account);
                            try {
                                $this->accounts->reconcile($account, $requestId);
                                $this->repository->succeeded($account);
                                $succeeded++;
                            } catch (ApiException $error) {
                                $permanent = in_array($error->errorCode, ['storage.invalid_key', 'configuration.invalid', 'configuration.missing', 'configuration.environment_mismatch'], true);
                                $failures = min(10, $account->reconcile_failures + 1);
                                $base = 60 * 2 ** min(4, $failures - 1);
                                $delay = max($error->retryAfter ?? 0, $base + random_int(0, min(30, intdiv($base, 4))));
                                $this->repository->failed($account, $error->errorCode, $failures, $permanent ? null : $delay);
                                $failed++;
                                $messages[] = $account->id.': '.$error->errorCode;
                            }
                        }
                    } while (count($batch) === 25);
                }
            }
        } finally {
            try {
                $this->scheduler->finished((int) (($this->clock->nanoseconds() - $started) / 1000000), $succeeded, $failed, $deferred, $budgetExhausted);
            } finally {
                $lock->release();
            }
        }

        return $messages;
    }
}
