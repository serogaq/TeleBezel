<?php

namespace App\Services;

use App\Contracts\Repositories\TelegramAccountRepository;
use App\Contracts\TdlibGateway;
use App\Data\AccountData;
use App\Enums\AccountLifecycle;
use App\Exceptions\ApiException;
use App\Support\Values;
use Illuminate\Pagination\LengthAwarePaginator;

final class TelegramAccountService
{
    public function __construct(private readonly TdlibGateway $tdlib, private readonly TelegramAccountRepository $repository, private readonly AccountStatePolicy $policy) {}

    /** @param array<string, mixed> $input
     * @return array{AccountData, bool}
     */
    public function create(string $apiClientId, string $idempotencyKey, array $input, string $requestId): array
    {
        return $this->createIdempotent(false, $apiClientId, $idempotencyKey, $input, $requestId);
    }

    /** @param array<string, mixed> $input
     * @return array{AccountData, bool}
     */
    public function createForOwner(string $instanceId, string $idempotencyKey, array $input, string $requestId): array
    {
        return $this->createIdempotent(true, $instanceId, $idempotencyKey, $input, $requestId);
    }

    /** @param array<string, mixed> $input
     * @return array{AccountData, bool} */
    private function createIdempotent(bool $owner, string $scopeId, string $idempotencyKey, array $input, string $requestId): array
    {
        if (strlen($idempotencyKey) < 8 || strlen($idempotencyKey) > 200) {
            throw new ApiException('request.invalid_idempotency_key', 422);
        }
        $proxy = $this->normalizedProxy(Values::object($input['proxy'] ?? [
            'mode' => 'inherit',
        ]));
        $requestHash = hash('sha256', $this->canonicalJson([
            'label' => $input['label'],
            'proxy' => $proxy,
        ]));
        [$account, $created] = $this->repository->create($owner, $scopeId, hash('sha256', $idempotencyKey), $requestHash, $input, $proxy);

        return [$this->repository->find($account->id), $created];
    }

    /** @return LengthAwarePaginator<int, AccountData> */
    public function paginate(int $perPage, string $requestId, int $page = 1): LengthAwarePaginator
    {
        $paginator = $this->repository->paginate(min(50, max(1, $perPage)), $page);
        $ids = $paginator->getCollection()->map(fn (AccountData $account): string => $account->id)->all();
        if ($ids !== []) {
            try {
                $snapshots = $this->tdlib->listSnapshots($ids, $requestId)['accounts'] ?? [];
                if (is_array($snapshots)) {
                    foreach ($paginator->getCollection() as $account) {
                        if (isset($snapshots[$account->id]) && is_array($snapshots[$account->id])) {
                            $this->policy->applySnapshot($account, Values::object($snapshots[$account->id]));
                            $this->persistSnapshotState($account);
                        }
                    }
                }
            } catch (ApiException) {
                // Durable application state remains readable while the runtime is unavailable.
                foreach ($paginator->getCollection() as $account) {
                    $account->runtime_available = false;
                }
            }
        }

        return $paginator;
    }

    public function find(string $id, bool $refresh, string $requestId): AccountData
    {
        $account = $this->repository->find($id);
        if ($refresh) {
            try {
                $this->policy->applySnapshot($account, $this->tdlib->snapshot($id, $requestId));
                $this->persistSnapshotState($account);
            } catch (ApiException) {
                $account->runtime_available = false;
            }
        }

        return $account;
    }

    /** @param array<string, mixed> $input */
    public function updateProxy(string $id, array $input, string $requestId): AccountData
    {
        $proxy = $this->normalizedProxy($input);
        $account = $this->repository->mutate($id, fn (AccountData $current): array => $this->policy->updateProxy($current, Values::integer($input['desired_revision']), $proxy));

        return $account;
    }

    /** @param array<string, mixed> $input
     * @return array<string, mixed>
     */
    public function authorizationAction(string $id, array $input, string $requestId): array
    {
        $account = $this->find($id, true, $requestId);
        if ($account->lifecycle === AccountLifecycle::LogoutPending) {
            throw new ApiException('operation.conflict', 409);
        }

        return $this->tdlib->authorizationAction($id, [
            ...$this->identity($account),
            'action' => $input['action'],
            'authorization_version' => $input['authorization_version'],
            ...array_key_exists('value', $input) ? [
                'value' => $input['value'],
            ] : [],
        ], $requestId);
    }

    public function logout(string $id, string $requestId): AccountData
    {
        return $this->repository->mutate($id, $this->policy->logout(...));
    }

    public function remove(string $id, string $requestId): ?AccountData
    {
        if ($this->repository->isRemoved($id)) {
            return null;
        }

        return $this->repository->mutate($id, $this->policy->remove(...));
    }

    /** @param array<string, mixed>|null $configuration */
    public function reconcile(AccountData $account, string $requestId, ?array $configuration = null): void
    {
        $configuration ??= $this->repository->configuration();
        try {
            $this->reconcileIntent($account, $requestId, $configuration);
        } catch (ApiException $exception) {
            if ($exception->errorCode !== 'operation.conflict') {
                throw $exception;
            }
            $refreshed = $this->find($account->id, true, $requestId);
            if ($refreshed->authorization_generation <= $account->authorization_generation) {
                throw $exception;
            }
            $this->reconcileIntent($refreshed, $requestId, $configuration);
        }
    }

    /** @param array<string, mixed> $configuration */
    private function reconcileIntent(AccountData $account, string $requestId, array $configuration): void
    {
        if ($account->lifecycle === AccountLifecycle::Removed) {
            return;
        }
        if ($account->lifecycle === AccountLifecycle::Removing) {
            try {
                $data = $this->tdlib->remove($account->id, $this->command($account, null, $configuration), $requestId);
            } catch (ApiException $exception) {
                if (! in_array($exception->errorCode, ['account.not_found', 'account.gone'], true)) {
                    throw $exception;
                }
                $data = [
                    'applied_revision' => $account->desired_revision,
                    'completed' => true,
                ];
            }
            $this->confirm($account, $data);

            return;
        }
        if ($account->lifecycle === AccountLifecycle::LogoutPending) {
            try {
                $this->confirm($account, $this->tdlib->logout($account->id, [
                    ...$this->command($account, null, $configuration),
                    'logout_operation_id' => $account->logout_operation_id,
                ], $requestId));
            } catch (ApiException $exception) {
                if ($exception->errorCode !== 'account.not_found') {
                    throw $exception;
                }
                $provisioning = $this->repository->mutate($account->id, fn (AccountData $current): array => $this->policy->abandonLogout($current, $account));
                if ($provisioning->lifecycle === AccountLifecycle::Provisioning) {
                    $this->dispatchProvision($provisioning, $requestId, false, deferFailure: false, configuration: $configuration);
                }
            }

            return;
        }
        $this->dispatchProvision($account, $requestId, $account->lifecycle === AccountLifecycle::Active, deferFailure: false, configuration: $configuration);
    }

    public function applyEffectiveProxy(AccountData $account, string $requestId): void
    {
        try {
            $account = $this->find($account->id, true, $requestId);
            if ($account->lifecycle !== AccountLifecycle::Active) {
                $this->reconcile($account, $requestId);

                return;
            }
            $this->confirm($account, $this->tdlib->updateProxy($account->id, $this->command($account), $requestId));
        } catch (ApiException $exception) {
            $this->recordDeferredError($account, $exception->errorCode);
        }
    }

    /** @return array<string, mixed> */
    public function proxy(AccountData $account): array
    {
        return [
            'id' => $account->proxy_id,
            'server' => $account->proxy_server,
            'port' => $account->proxy_port,
            'type' => $account->proxy_type,
            'desired_revision' => $account->desired_revision,
            'applied_revision' => $account->applied_revision,
        ];
    }

    /** @param array<string, mixed>|null $proxy
     * @param array<string, mixed>|null $configuration */
    private function dispatchProvision(AccountData $account, string $requestId, bool $restore, ?array $proxy = null, bool $deferFailure = true, ?array $configuration = null): void
    {
        try {
            $data = $this->tdlib->provision($account->id, [
                ...$this->command($account, $proxy, $configuration),
                'mode' => $restore ? 'restore' : 'create',
            ], $requestId);
            $this->confirm($account, $data);
        } catch (ApiException $exception) {
            $this->repository->recordDeferredError($account, $exception->errorCode);
            if (! $deferFailure) {
                throw $exception;
            }
        }
    }

    private function recordDeferredError(AccountData $account, string $code): void
    {
        $this->repository->recordDeferredError($account, $code);
    }

    /** @param array<string, mixed> $data */
    private function confirm(AccountData $account, array $data): void
    {
        $this->repository->mutate($account->id, fn (AccountData $current): array => $this->policy->confirm($current, $account, $data));
    }

    private function persistSnapshotState(AccountData $account): void
    {
        $this->repository->persistSnapshotState($account);
    }

    /** @param array<string, mixed> $proxy
     * @return array<string, mixed>
     */
    private function normalizedProxy(array $proxy): array
    {
        $mode = Values::string($proxy['mode'] ?? 'inherit');
        if (! in_array($mode, ['inherit', 'direct', 'socks5', 'http', 'mtproto'], true)) {
            throw new ApiException('configuration.invalid', 422);
        }
        if ($mode !== 'inherit' && ($proxy['id'] ?? '') === '') {
            throw new ApiException('configuration.invalid', 422);
        }
        if ($mode === 'inherit' && array_intersect(['id', 'host', 'port', 'http_only', 'username', 'password', 'secret'], array_keys($proxy)) !== []) {
            throw new ApiException('configuration.invalid', 422);
        }
        if (($proxy['password'] ?? '') !== '' && ($proxy['username'] ?? '') === '') {
            throw new ApiException('configuration.invalid', 422);
        }

        return [
            ...$mode === 'inherit' ? [] : [
                'id' => Values::string($proxy['id']),
            ],
            'mode' => $mode,
            ...isset($proxy['host']) ? [
                'host' => Values::string($proxy['host']),
            ] : [],
            ...isset($proxy['port']) ? [
                'port' => Values::integer($proxy['port']),
            ] : [],
            'http_only' => $mode === 'http' && ($proxy['http_only'] ?? false) === true,
            ...isset($proxy['username']) ? [
                'username' => Values::string($proxy['username']),
            ] : [],
            ...isset($proxy['password']) ? [
                'password' => Values::string($proxy['password']),
            ] : [],
            ...isset($proxy['secret']) ? [
                'secret' => Values::string($proxy['secret']),
            ] : [],
        ];
    }

    /** @return array<string, mixed> */
    private function identity(AccountData $account): array
    {
        return [
            'uuid' => $account->id,
            'generation' => $account->storage_generation,
            'authorization_generation' => $account->authorization_generation,
            'revision' => $account->desired_revision,
        ];
    }

    /** @param array<string, mixed>|null $proxy
     * @param  array<string, mixed>|null  $configuration
     * @return array<string, mixed>
     */
    private function command(AccountData $account, ?array $proxy = null, ?array $configuration = null): array
    {
        $config = $configuration ?? $this->repository->configuration();
        $globalProxy = Values::object($config['proxy']);
        if ($proxy === null) {
            $stored = $account->proxy_config;
            if ($account->proxy_id === null) {
                $proxy = $globalProxy;
            } elseif ($stored === null) {
                // Without the stored configuration the runtime would be handed
                // a profile stripped of its credentials.
                throw new ApiException('configuration.missing', 409);
            } else {
                $proxy = [
                    ...$stored,
                    'version' => $account->proxy_config_version,
                ];
            }
        }
        $credentials = $config['telegram_api_id'] !== null && $config['telegram_api_hash'] !== null ? [
            'telegram_api_id' => $config['telegram_api_id'],
            'telegram_api_hash' => $config['telegram_api_hash'],
        ] : [];

        return [
            ...$this->identity($account),
            'effective_config_id' => $account->effective_config_id,
            ...$credentials,
            'operation_id' => $account->operation_id,
            'lifecycle' => $account->lifecycle->value,
            'proxy' => $proxy,
        ];
    }

    /** @param array<string, mixed> $value */
    private function canonicalJson(array $value): string
    {
        $sort = function (&$item) use (&$sort): void {
            if (! is_array($item)) {
                return;
            }
            if (! array_is_list($item)) {
                ksort($item);
            }
            foreach ($item as &$child) {
                $sort($child);
            }
        };
        $sort($value);

        return json_encode($value, JSON_THROW_ON_ERROR | JSON_UNESCAPED_SLASHES);
    }
}
