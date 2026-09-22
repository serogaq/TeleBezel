<?php

declare(strict_types=1);

namespace App\Services;

use App\Cache\ReconciliationCacheKeys;
use App\Contracts\MonotonicClock;
use App\Contracts\Repositories\SchedulerRepository;
use App\Contracts\Repositories\TelegramAccountRepository;
use App\Contracts\TdlibGateway;
use App\Data\AccountData;
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
            $configuration = $this->repository->configuration();
            foreach ([['logout_pending', 'removing', 'provisioning'], ['active']] as $phase => $lifecycles) {
                $deferred += $this->repository->deferredCount($lifecycles);
                $key = ReconciliationCacheKeys::cursor($phase);
                $saved = Cache::get($key);
                $saved = is_string($saved) ? $saved : null;
                foreach ($saved === null ? [[null, null]] : [[$saved, null], [null, $saved]] as [$after, $through]) {
                    do {
                        $batch = $this->repository->batch($lifecycles, $after, $through);
                        $healthy = $phase === 1 ? $this->healthy($batch, $requestId) : [];
                        foreach ($batch as $account) {
                            if (($this->clock->nanoseconds() - $started) / 1000000000 >= 35) {
                                $budgetExhausted = true;
                                break 4;
                            }
                            $after = $account->id;
                            if (isset($healthy[$account->id])) {
                                $succeeded++;

                                continue;
                            }
                            Cache::forever($key, $after);
                            $this->repository->attempted($account);
                            try {
                                $this->accounts->reconcile($account, $requestId, $configuration);
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
                        if ($batch !== []) {
                            Cache::forever($key, $after);
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

    /** @param array<int, AccountData> $batch
     * @return array<string, true> */
    private function healthy(array $batch, string $requestId): array
    {
        if ($batch === []) {
            return [];
        }
        try {
            $snapshots = $this->tdlib->listSnapshots(array_map(fn (AccountData $account): string => $account->id, $batch), $requestId)['accounts'] ?? [];
        } catch (ApiException) {
            return [];
        }
        if (! is_array($snapshots)) {
            return [];
        }
        $healthy = [];
        foreach ($batch as $account) {
            $snapshot = $snapshots[$account->id] ?? null;
            if (is_array($snapshot)
                && ($snapshot['runtime_available'] ?? false) === true
                && ($snapshot['generation'] ?? null) === $account->storage_generation
                && ($snapshot['target_revision'] ?? null) === $account->desired_revision
                && ($snapshot['applied_revision'] ?? null) === $account->desired_revision
                && ($snapshot['authorization_generation'] ?? null) === $account->authorization_generation
                && ($snapshot['effective_config_id'] ?? null) === $account->effective_config_id
                && ($snapshot['operation_id'] ?? null) === null
                && ($snapshot['last_error_code'] ?? null) === null
                && $account->applied_revision === $account->desired_revision) {
                $healthy[$account->id] = true;
            }
        }

        return $healthy;
    }
}
