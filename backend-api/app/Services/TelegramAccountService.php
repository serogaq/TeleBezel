<?php

namespace App\Services;

use App\Enums\AccountLifecycle;
use App\Exceptions\ApiException;
use App\Models\AccountIdempotencyKey;
use App\Models\Instance;
use App\Models\OwnerAccountIdempotencyKey;
use App\Models\ProxyProfile;
use App\Models\TelegramAccount;
use Illuminate\Database\QueryException;
use Illuminate\Pagination\LengthAwarePaginator;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Str;

final class TelegramAccountService
{
    public function __construct(private readonly TdlibGateway $tdlib) {}

    /** @param array<string, mixed> $input
     * @return array{TelegramAccount, bool}
     */
    public function create(string $apiClientId, string $idempotencyKey, array $input, string $requestId): array
    {
        return $this->createIdempotent(AccountIdempotencyKey::class, 'api_client_id', $apiClientId, $idempotencyKey, $input, $requestId);
    }

    /** @param array<string, mixed> $input
     * @return array{TelegramAccount, bool}
     */
    public function createForOwner(string $ownerSessionId, string $idempotencyKey, array $input, string $requestId): array
    {
        return $this->createIdempotent(OwnerAccountIdempotencyKey::class, 'owner_session_id', $ownerSessionId, $idempotencyKey, $input, $requestId);
    }

    /** @param class-string<AccountIdempotencyKey|OwnerAccountIdempotencyKey> $keyModel
     * @param  array<string, mixed>  $input
     * @return array{TelegramAccount, bool}
     */
    private function createIdempotent(string $keyModel, string $scopeColumn, string $scopeId, string $idempotencyKey, array $input, string $requestId): array
    {
        if (strlen($idempotencyKey) < 8 || strlen($idempotencyKey) > 200) {
            throw new ApiException('request.invalid_idempotency_key', 422);
        }
        $keyHash = hash('sha256', $idempotencyKey);
        $proxy = $this->normalizedProxy($input['proxy'] ?? ['mode' => 'inherit']);
        $requestHash = hash('sha256', $this->canonicalJson(['label' => $input['label'], 'proxy' => $proxy]));
        try {
            [$account, $created] = DB::transaction(function () use ($keyModel, $scopeColumn, $scopeId, $keyHash, $requestHash, $input, $proxy): array {
                $existing = $keyModel::query()
                    ->where($scopeColumn, $scopeId)->where('key_hash', $keyHash)->lockForUpdate()->first();
                if ($existing !== null) {
                    if (! hash_equals($existing->request_hash, $requestHash)) {
                        throw new ApiException('operation.conflict', 409);
                    }
                    $account = TelegramAccount::withTrashed()->findOrFail($existing->telegram_account_id);
                    if ($account->lifecycle === AccountLifecycle::Removed) {
                        throw new ApiException('account.gone', 410);
                    }

                    return [$account, false];
                }
                $account = TelegramAccount::query()->create([
                    'id' => (string) Str::uuid(),
                    'label' => $input['label'],
                    'storage_generation' => (string) Str::uuid(),
                    'lifecycle' => AccountLifecycle::Provisioning,
                    'desired_revision' => 1,
                    'effective_config_id' => (string) Str::uuid(),
                    'proxy_id' => $proxy['id'] ?? null,
                    'proxy_server' => $proxy['host'] ?? null,
                    'proxy_port' => $proxy['port'] ?? null,
                    'proxy_type' => $proxy['mode'] === 'inherit' ? null : $proxy['mode'],
                ]);
                $keyModel::query()->create([
                    $scopeColumn => $scopeId,
                    'key_hash' => $keyHash,
                    'request_hash' => $requestHash,
                    'telegram_account_id' => $account->id,
                ]);

                return [$account, true];
            }, 3);
        } catch (QueryException $exception) {
            if ($exception->getCode() !== '23505') {
                throw $exception;
            }

            return $this->createIdempotent($keyModel, $scopeColumn, $scopeId, $idempotencyKey, $input, $requestId);
        }

        if ($created) {
            $this->dispatchProvision($account, $requestId, false, $proxy);
        }

        return [$account->fresh(), $created];
    }

    /** @return LengthAwarePaginator<int, TelegramAccount> */
    public function paginate(int $perPage, string $requestId): LengthAwarePaginator
    {
        $paginator = TelegramAccount::query()->where('lifecycle', '!=', AccountLifecycle::Removed->value)
            ->orderBy('created_at')->paginate(min(50, max(1, $perPage)));
        $ids = $paginator->getCollection()->pluck('id')->all();
        if ($ids !== []) {
            try {
                $snapshots = $this->tdlib->listSnapshots($ids, $requestId)['accounts'] ?? [];
                if (is_array($snapshots)) {
                    foreach ($paginator->getCollection() as $account) {
                        if (isset($snapshots[$account->id]) && is_array($snapshots[$account->id])) {
                            $this->applySnapshot($account, $snapshots[$account->id]);
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

    public function find(string $id, bool $refresh, string $requestId): TelegramAccount
    {
        if (! Str::isUuid($id)) {
            throw new ApiException('account.not_found', 404);
        }
        $account = TelegramAccount::withTrashed()->find($id);
        if ($account === null) {
            throw new ApiException('account.not_found', 404);
        }
        if ($account->lifecycle === AccountLifecycle::Removed) {
            throw new ApiException('account.gone', 410);
        }
        if ($refresh) {
            try {
                $this->applySnapshot($account, $this->tdlib->snapshot($id, $requestId));
                $this->persistSnapshotState($account);
            } catch (ApiException) {
                $account->runtime_available = false;
            }
        }

        return $account;
    }

    /** @param array<string, mixed> $input */
    public function updateProxy(string $id, array $input, string $requestId): TelegramAccount
    {
        $proxy = $this->normalizedProxy($input);

        $account = DB::transaction(function () use ($id, $input, $proxy): TelegramAccount {
            $account = TelegramAccount::query()->whereKey($id)->lockForUpdate()->first();
            if ($account === null || $account->lifecycle === AccountLifecycle::Removed) {
                throw new ApiException($account === null ? 'account.not_found' : 'account.gone', $account === null ? 404 : 410);
            }
            if ($account->desired_revision !== (int) $input['desired_revision']) {
                throw new ApiException('operation.conflict', 409);
            }
            $account->proxy_id = $proxy['id'] ?? null;
            $account->proxy_server = $proxy['host'] ?? null;
            $account->proxy_port = $proxy['port'] ?? null;
            $account->proxy_type = $proxy['mode'] === 'inherit' ? null : $proxy['mode'];
            $account->desired_revision++;
            $account->effective_config_id = (string) Str::uuid();
            $account->operation_id = (string) Str::uuid();
            $account->last_error_code = null;
            $account->save();

            return $account;
        }, 3);
        $this->confirm($account, $this->tdlib->updateProxy($id, $this->command($account, $proxy), $requestId));

        return $account->fresh();
    }

    /** @param array<string, mixed> $input
     * @return array<string, mixed>
     */
    public function authorizationAction(string $id, array $input, string $requestId): array
    {
        $account = $this->find($id, false, $requestId);
        if ($account->lifecycle === AccountLifecycle::LogoutPending) {
            throw new ApiException('operation.conflict', 409);
        }

        return $this->tdlib->authorizationAction($id, [
            ...$this->identity($account),
            'action' => $input['action'],
            'authorization_version' => $input['authorization_version'],
            ...array_key_exists('value', $input) ? ['value' => $input['value']] : [],
        ], $requestId);
    }

    public function logout(string $id, string $requestId): TelegramAccount
    {
        $account = DB::transaction(function () use ($id): TelegramAccount {
            $account = TelegramAccount::query()->whereKey($id)->lockForUpdate()->first();
            if ($account === null) {
                throw new ApiException('account.not_found', 404);
            }
            if ($account->lifecycle === AccountLifecycle::Removed) {
                throw new ApiException('account.gone', 410);
            }
            if ($account->lifecycle !== AccountLifecycle::LogoutPending) {
                $account->lifecycle = AccountLifecycle::LogoutPending;
                $account->desired_revision++;
                $account->authorization_generation++;
                $account->logout_operation_id = (string) Str::uuid();
                $account->operation_id = $account->logout_operation_id;
                $account->logout_completed_at = null;
                $account->save();
            }

            return $account;
        }, 3);
        try {
            $data = $this->tdlib->logout($id, [...$this->command($account), 'logout_operation_id' => $account->logout_operation_id], $requestId);
        } catch (ApiException $exception) {
            if ($exception->errorCode !== 'service.tdlib_unavailable') {
                throw $exception;
            }
            $this->recordDeferredError($account, $exception->errorCode);

            return $account->fresh();
        }
        $this->confirm($account, $data);

        return $account->fresh();
    }

    public function remove(string $id, string $requestId): ?TelegramAccount
    {
        $existing = TelegramAccount::withTrashed()->find($id);
        if ($existing?->lifecycle === AccountLifecycle::Removed) {
            return null;
        }
        $account = DB::transaction(function () use ($id): TelegramAccount {
            $account = TelegramAccount::query()->whereKey($id)->lockForUpdate()->first();
            if ($account === null) {
                throw new ApiException('account.not_found', 404);
            }
            if ($account->lifecycle !== AccountLifecycle::Removing) {
                $account->lifecycle = AccountLifecycle::Removing;
                $account->desired_revision++;
                $account->operation_id = (string) Str::uuid();
                $account->save();
            }

            return $account;
        }, 3);
        try {
            $data = $this->tdlib->remove($id, $this->command($account), $requestId);
        } catch (ApiException $exception) {
            if ($exception->errorCode !== 'service.tdlib_unavailable') {
                throw $exception;
            }
            $this->recordDeferredError($account, $exception->errorCode);

            return $account->fresh();
        }
        $this->confirm($account, $data);

        $confirmed = TelegramAccount::withTrashed()->findOrFail($account->id);

        return $confirmed->lifecycle === AccountLifecycle::Removed ? null : $confirmed;
    }

    public function reconcile(TelegramAccount $account, string $requestId): void
    {
        if ($account->lifecycle === AccountLifecycle::Removed) {
            return;
        }
        if ($account->lifecycle === AccountLifecycle::Removing) {
            $this->confirm($account, $this->tdlib->remove($account->id, $this->command($account), $requestId));

            return;
        }
        if ($account->lifecycle === AccountLifecycle::LogoutPending) {
            $this->confirm($account, $this->tdlib->logout($account->id, [...$this->command($account), 'logout_operation_id' => $account->logout_operation_id], $requestId));

            return;
        }
        $this->dispatchProvision($account, $requestId, $account->lifecycle === AccountLifecycle::Active);
    }

    public function applyEffectiveProxy(TelegramAccount $account, string $requestId): void
    {
        if ($account->lifecycle !== AccountLifecycle::Active) {
            $this->reconcile($account, $requestId);

            return;
        }
        try {
            $this->confirm($account, $this->tdlib->updateProxy($account->id, $this->command($account), $requestId));
        } catch (ApiException $exception) {
            $this->recordDeferredError($account, $exception->errorCode);
        }
    }

    /** @return array<string, mixed> */
    public function proxy(TelegramAccount $account): array
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

    /** @param array<string, mixed>|null $proxy */
    private function dispatchProvision(TelegramAccount $account, string $requestId, bool $restore, ?array $proxy = null): void
    {
        try {
            $data = $this->tdlib->provision($account->id, [...$this->command($account, $proxy), 'mode' => $restore ? 'restore' : 'create'], $requestId);
            $this->confirm($account, $data);
        } catch (ApiException $exception) {
            TelegramAccount::query()->whereKey($account->id)->where('desired_revision', $account->desired_revision)
                ->update(['last_error_code' => $exception->errorCode, 'runtime_available' => false]);
        }
    }

    private function recordDeferredError(TelegramAccount $account, string $code): void
    {
        TelegramAccount::query()->whereKey($account->id)->where('desired_revision', $account->desired_revision)
            ->update(['last_error_code' => $code, 'runtime_available' => false]);
    }

    /** @param array<string, mixed> $data */
    private function confirm(TelegramAccount $account, array $data): void
    {
        DB::transaction(function () use ($account, $data): void {
            $locked = TelegramAccount::withTrashed()->whereKey($account->id)->lockForUpdate()->first();
            if ($locked === null || $locked->desired_revision !== $account->desired_revision) {
                return;
            }
            if (isset($data['effective_config_id']) && $data['effective_config_id'] !== $locked->effective_config_id) {
                return;
            }
            $applied = $data['applied_revision'] ?? null;
            if (is_int($applied) && $applied === $account->desired_revision) {
                $locked->applied_revision = $applied;
            }
            if (($data['completed'] ?? false) === true && $locked->lifecycle === AccountLifecycle::Removing) {
                $locked->fill(['label' => null, 'proxy_id' => null, 'proxy_server' => null, 'proxy_port' => null, 'proxy_type' => null, 'lifecycle' => AccountLifecycle::Removed, 'runtime_available' => false, 'authorization_state' => null, 'connection_state' => null, 'last_error_code' => null]);
                $locked->delete();
            } elseif (($data['completed'] ?? false) === true && $locked->lifecycle === AccountLifecycle::LogoutPending) {
                $locked->lifecycle = AccountLifecycle::Active;
                $locked->logout_completed_at = now();
                $locked->operation_id = null;
            } elseif ($locked->lifecycle === AccountLifecycle::Provisioning && $locked->applied_revision === $locked->desired_revision) {
                $locked->lifecycle = AccountLifecycle::Active;
                $locked->operation_id = null;
            } elseif (($data['completed'] ?? false) === true && $locked->lifecycle === AccountLifecycle::Active) {
                $locked->operation_id = null;
            }
            $this->applySnapshot($locked, $data, false);
            $locked->save();
        }, 3);
    }

    /** @param array<string, mixed> $snapshot */
    private function applySnapshot(TelegramAccount $account, array $snapshot, bool $includeIdentity = true): void
    {
        $applied = $snapshot['applied_revision'] ?? null;
        $effectiveConfigId = $snapshot['effective_config_id'] ?? null;
        if (is_int($applied) && $applied === $account->desired_revision
            && is_string($effectiveConfigId) && hash_equals((string) $account->effective_config_id, $effectiveConfigId)) {
            $account->applied_revision = $applied;
            if (($snapshot['operation_id'] ?? null) === null) {
                $account->operation_id = null;
            }
        }
        if (array_key_exists('runtime_available', $snapshot)) {
            $account->runtime_available = $snapshot['runtime_available'] === true;
        }
        foreach (['authorization_state', 'connection_state', 'last_error_code'] as $field) {
            if (array_key_exists($field, $snapshot) && (is_string($snapshot[$field]) || $snapshot[$field] === null)) {
                $account->{$field} = $snapshot[$field];
            }
        }
        if ($includeIdentity && isset($snapshot['telegram_identity']) && is_array($snapshot['telegram_identity'])) {
            unset($snapshot['telegram_identity']['phone_number']);
            $account->telegram_identity = $snapshot['telegram_identity'];
        }
    }

    private function persistSnapshotState(TelegramAccount $account): void
    {
        TelegramAccount::query()->whereKey($account->id)->update([
            'applied_revision' => $account->applied_revision,
            'runtime_available' => $account->runtime_available,
            'authorization_state' => $account->authorization_state,
            'connection_state' => $account->connection_state,
            'last_error_code' => $account->last_error_code,
            'operation_id' => $account->operation_id,
        ]);
    }

    /** @param array<string, mixed> $proxy
     * @return array<string, mixed>
     */
    private function normalizedProxy(array $proxy): array
    {
        $mode = (string) ($proxy['mode'] ?? 'inherit');
        if (! in_array($mode, ['inherit', 'direct', 'socks5', 'http', 'mtproto'], true)) {
            throw new ApiException('configuration.invalid', 422);
        }
        if ($mode !== 'inherit' && empty($proxy['id'])) {
            throw new ApiException('configuration.invalid', 422);
        }
        if (! empty($proxy['password']) && empty($proxy['username'])) {
            throw new ApiException('configuration.invalid', 422);
        }

        return [
            ...$mode === 'inherit' ? [] : ['id' => (string) $proxy['id']],
            'mode' => $mode,
            ...isset($proxy['host']) ? ['host' => (string) $proxy['host']] : [],
            ...isset($proxy['port']) ? ['port' => (int) $proxy['port']] : [],
            'http_only' => $mode === 'http' && ($proxy['http_only'] ?? false) === true,
            ...isset($proxy['username']) ? ['username' => (string) $proxy['username']] : [],
            ...isset($proxy['password']) ? ['password' => (string) $proxy['password']] : [],
            ...isset($proxy['secret']) ? ['secret' => (string) $proxy['secret']] : [],
        ];
    }

    /** @return array<string, mixed> */
    private function identity(TelegramAccount $account): array
    {
        return ['uuid' => $account->id, 'generation' => $account->storage_generation,
            'authorization_generation' => $account->authorization_generation, 'revision' => $account->desired_revision];
    }

    /** @return array<string, mixed> */
    /** @param array<string, mixed>|null $proxy
     * @return array<string, mixed>
     */
    private function command(TelegramAccount $account, ?array $proxy = null): array
    {
        $instance = Instance::query()->first();
        $profile = $instance?->active_proxy_profile_id === null ? null : ProxyProfile::query()->find($instance->active_proxy_profile_id);
        $globalProxy = $profile instanceof ProxyProfile ? ['id' => $profile->id, 'mode' => $profile->mode,
            'host' => $profile->host, 'port' => $profile->port, 'http_only' => $profile->http_only,
            ...($profile->username ? ['username' => $profile->username] : []), ...($profile->credentials ?? [])] : ['mode' => 'direct'];
        $proxy ??= $account->proxy_id === null ? $globalProxy : [
            'id' => $account->proxy_id,
            'mode' => $account->proxy_type ?? 'direct',
            ...$account->proxy_server === null ? [] : ['host' => $account->proxy_server],
            ...$account->proxy_port === null ? [] : ['port' => $account->proxy_port],
        ];
        $credentials = $instance?->telegram_api_id !== null && $instance->telegram_api_hash !== null
            ? ['telegram_api_id' => $instance->telegram_api_id, 'telegram_api_hash' => $instance->telegram_api_hash]
            : [];

        return [...$this->identity($account), 'effective_config_id' => $account->effective_config_id,
            ...$credentials,
            'operation_id' => $account->operation_id, 'lifecycle' => $account->lifecycle->value, 'proxy' => $proxy];
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
