<?php

declare(strict_types=1);

namespace App\Services;

use App\Cache\ProxyCacheKeys;
use App\Contracts\Repositories\ProxyProfileRepository;
use App\Contracts\TdlibGateway;
use App\Contracts\TransactionManager;
use App\Data\Input;
use App\Data\ProxyProfileData;
use App\Exceptions\ApiException;
use App\Support\Values;
use Illuminate\Support\Facades\Cache;

final readonly class ProxyProfileService
{
    public function __construct(private ProxyProfileRepository $profiles, private TdlibGateway $tdlib, private TelegramAccountService $accounts, private TransactionManager $transactions) {}

    /** @return array<int, array<string, mixed>> */
    public function all(string $instanceId): array
    {
        $active = $this->profiles->policy($instanceId)?->activeId;

        return array_map(fn (ProxyProfileData $profile): array => $profile->publicData($active === $profile->id), $this->profiles->all($instanceId));
    }

    /** @return array<string, mixed> */
    public function create(string $instanceId, Input $input, string $requestId): array
    {
        $profile = $this->transactions->run(function () use ($instanceId, $input): ProxyProfileData {
            $this->profiles->lockInstance($instanceId);
            if (count($this->profiles->all($instanceId)) >= 20) {
                throw new ApiException('proxy.limit_reached', 422);
            }

            return $this->profiles->create($instanceId, $input);
        });

        return $this->ping($instanceId, $profile->id, $requestId);
    }

    /** @return array<string, mixed> */
    public function ping(string $instanceId, string $id, string $requestId): array
    {
        $profile = $this->profiles->find($instanceId, $id);
        $accountId = $this->profiles->runtimeAccount();
        if ($accountId === null) {
            $profile = $this->profiles->recordPing($profile, false, null, 'proxy.no_runtime');
        } else {
            try {
                $result = $this->tdlib->pingProxy($accountId, $profile->definition(), $requestId);
                $profile = $this->profiles->recordPing($profile, true, Values::integer($result['latency_ms'] ?? null), null);
            } catch (ApiException $error) {
                $profile = $this->profiles->recordPing($profile, false, null, $error->errorCode);
            }
        }

        return $profile->publicData($this->profiles->policy($instanceId)?->activeId === $id);
    }

    /** @return array<int, array<string, mixed>> */
    public function pingAll(string $instanceId, string $requestId): array
    {
        return array_map(fn (ProxyProfileData $profile): array => $this->ping($instanceId, $profile->id, $requestId), $this->profiles->all($instanceId));
    }

    public function delete(string $instanceId, string $id): void
    {
        $this->transactions->run(function () use ($instanceId, $id): void {
            $this->profiles->lockInstance($instanceId);
            if ($this->profiles->policy($instanceId)?->activeId === $id) {
                throw new ApiException('proxy.active', 409);
            }
            $this->profiles->delete($instanceId, $id);
        });
    }

    public function activate(string $instanceId, ?string $id, string $requestId): void
    {
        foreach ($this->profiles->activate($instanceId, $id) as $accountId) {
            $this->accounts->applyEffectiveProxy($this->accounts->find($accountId, false, $requestId), $requestId);
        }
    }

    public function configure(string $instanceId, Input $input): void
    {
        $this->profiles->configure($instanceId, $input);
    }

    public function monitor(string $requestId): void
    {
        $lock = Cache::lock(ProxyCacheKeys::monitorLock(), 10);
        if ($lock->get() !== true) {
            return;
        }
        try {
            $policy = $this->profiles->policy();
            if ($policy === null || $policy->activeId === null) {
                return;
            }
            $ids = $this->profiles->inheritedAccounts();
            if ($ids === []) {
                return;
            }
            try {
                $snapshots = Values::object($this->tdlib->listSnapshots($ids, $requestId)['accounts'] ?? []);
            } catch (ApiException) {
                return;
            }
            foreach ($ids as $id) {
                $snapshot = $snapshots[$id] ?? null;
                if (is_array($snapshot) && ($snapshot['connection_state'] ?? null) === 'ready') {
                    if ($policy->failureStartedAt !== null) {
                        $this->profiles->recordFailure($policy, false);
                    }

                    return;
                }
            }
            if ($policy->failureStartedAt === null) {
                $this->profiles->recordFailure($policy, true);

                return;
            }
            if ($policy->failureStartedAt->addSeconds($policy->timeoutSeconds)->isFuture()) {
                return;
            }
            $next = null;
            if ($policy->failureAction === 'next') {
                $all = $this->profiles->all($policy->instanceId);
                $current = $this->profiles->find($policy->instanceId, $policy->activeId);
                foreach ($all as $profile) {
                    if ($profile->position > $current->position) {
                        $next = $profile->id;
                        break;
                    }
                }
                if ($next === null) {
                    foreach ($all as $profile) {
                        if ($profile->id !== $current->id) {
                            $next = $profile->id;
                            break;
                        }
                    }
                }
            }
            $this->activate($policy->instanceId, $next, $requestId);
        } finally {
            $lock->release();
        }
    }
}
