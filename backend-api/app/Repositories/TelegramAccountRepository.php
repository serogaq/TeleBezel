<?php

declare(strict_types=1);

namespace App\Repositories;

use App\Contracts\Repositories\TelegramAccountRepository as TelegramAccountRepositoryContract;
use App\Data\AccountData;
use App\Enums\AccountLifecycle;
use App\Exceptions\ApiException;
use App\Models\AccountIdempotencyKey;
use App\Models\Instance;
use App\Models\ProxyProfile;
use App\Models\TelegramAccount;
use App\Support\Values;
use Carbon\CarbonImmutable;
use Closure;
use Illuminate\Database\Eloquent\Builder;
use Illuminate\Database\QueryException;
use Illuminate\Pagination\LengthAwarePaginator;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Str;

final class TelegramAccountRepository implements TelegramAccountRepositoryContract
{
    private function data(TelegramAccount $account): AccountData
    {
        return new AccountData($account->id, $account->label, $account->storage_generation, $account->lifecycle, $account->desired_revision, $account->applied_revision, $account->proxy_id, $account->proxy_server, $account->proxy_port, $account->proxy_type, $account->proxy_config, $account->proxy_config_version, $account->runtime_available, $account->authorization_state, $account->connection_state, $account->last_error_code, $account->operation_id, $account->logout_operation_id, $account->authorization_generation, $account->effective_config_id, $account->created_at === null ? null : CarbonImmutable::instance($account->created_at), $account->updated_at === null ? null : CarbonImmutable::instance($account->updated_at), $account->next_reconcile_at === null ? null : CarbonImmutable::instance($account->next_reconcile_at), $account->reconcile_failures, $account->reconcile_blocked_revision);
    }

    /** @param array<string, mixed> $input
     * @param array<string, mixed> $proxy
     * @return array{AccountData, bool} */
    public function create(string $tokenId, string $keyHash, string $requestHash, array $input, array $proxy, int $attempt = 0): array
    {
        try {
            [$account, $created] = DB::transaction(function () use ($tokenId, $keyHash, $requestHash, $input, $proxy): array {
                $existing = AccountIdempotencyKey::query()->where('token_id', $tokenId)->where('key_hash', $keyHash)->lockForUpdate()->first();
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
                    'proxy_id' => isset($proxy['id']) ? Values::string($proxy['id']) : null,
                    'proxy_server' => isset($proxy['host']) ? Values::string($proxy['host']) : null,
                    'proxy_port' => isset($proxy['port']) ? Values::integer($proxy['port']) : null,
                    'proxy_type' => $proxy['mode'] === 'inherit' ? null : $proxy['mode'],
                    'proxy_config' => $proxy['mode'] === 'inherit' ? null : $proxy,
                    'proxy_config_version' => $proxy['mode'] === 'inherit' ? 0 : 1,
                ]);
                AccountIdempotencyKey::query()->create([
                    'token_id' => $tokenId,
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
            if ($attempt >= 2) {
                throw new ApiException('operation.conflict', 409);
            }

            return $this->create($tokenId, $keyHash, $requestHash, $input, $proxy, $attempt + 1);
        }

        return [$this->data($account->fresh() ?? $account), $created];
    }

    public function find(string $id): AccountData
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

        return $this->data($account);
    }

    /** @param list<string>|null $only
     * @return LengthAwarePaginator<int, AccountData> */
    public function paginate(int $perPage, int $page, ?array $only = null): LengthAwarePaginator
    {
        $query = TelegramAccount::query()->where('lifecycle', '!=', AccountLifecycle::Removed->value);
        if ($only !== null) {
            $query->whereKey($only);
        }

        return $query->orderBy('created_at')->paginate($perPage, ['*'], 'page', $page)->through(fn (TelegramAccount $account): AccountData => $this->data($account));
    }

    public function isRemoved(string $id): bool
    {
        return TelegramAccount::withTrashed()->find($id)?->lifecycle === AccountLifecycle::Removed;
    }

    /** @param Closure(AccountData): array<string, mixed> $transition */
    public function mutate(string $id, Closure $transition): AccountData
    {
        return DB::transaction(function () use ($id, $transition): AccountData {
            $model = TelegramAccount::withTrashed()->whereKey($id)->lockForUpdate()->first();
            if ($model === null) {
                throw new ApiException('account.not_found', 404);
            }
            $changes = $transition($this->data($model));
            if ($changes !== []) {
                $model->forceFill($changes)->save();
                if ($model->lifecycle === AccountLifecycle::Removed && ! $model->trashed()) {
                    $model->delete();
                }
            }

            return $this->data($model);
        }, 3);
    }

    public function persistSnapshotState(AccountData $account): void
    {
        TelegramAccount::query()->whereKey($account->id)->where('storage_generation', $account->storage_generation)->where('desired_revision', $account->desired_revision)->where('effective_config_id', $account->effective_config_id)->where('authorization_generation', '<=', $account->authorization_generation)->update([
            'applied_revision' => $account->applied_revision,
            'runtime_available' => $account->runtime_available,
            'authorization_state' => $account->authorization_state,
            'connection_state' => $account->connection_state,
            'last_error_code' => $account->last_error_code,
            'operation_id' => $account->operation_id,
            'authorization_generation' => $account->authorization_generation,
        ]);
    }

    public function recordDeferredError(AccountData $account, string $code): void
    {
        TelegramAccount::query()->whereKey($account->id)->where('desired_revision', $account->desired_revision)->update([
            'last_error_code' => $code,
            'runtime_available' => false,
        ]);
    }

    /** @return array<string, mixed> */
    public function configuration(): array
    {
        $instance = Instance::query()->first();
        $profile = $instance?->active_proxy_profile_id === null ? null : ProxyProfile::query()->find($instance->active_proxy_profile_id);
        $globalProxy = $profile instanceof ProxyProfile ? [
            'id' => $profile->id,
            'mode' => $profile->mode,
            'host' => $profile->host,
            'port' => $profile->port,
            'http_only' => $profile->http_only,
            ...$profile->username !== null && $profile->username !== '' ? [
                'username' => $profile->username,
            ] : [],
            ...$profile->credentials ?? [],
        ] : [
            'mode' => 'direct',
        ];

        return [
            'proxy' => $globalProxy,
            'telegram_api_id' => $instance?->telegram_api_id,
            'telegram_api_hash' => $instance?->telegram_api_hash,
        ];
    }

    public function usesProxy(AccountData $account): bool
    {
        if ($account->proxy_type !== null) {
            return $account->proxy_type !== 'direct';
        }
        $instance = Instance::query()->first();
        if ($instance?->active_proxy_profile_id === null) {
            return false;
        }
        $profile = ProxyProfile::query()->find($instance->active_proxy_profile_id);

        return $profile instanceof ProxyProfile && $profile->mode !== 'direct';
    }

    /** @param list<string> $lifecycles
     * @return array<int, AccountData> */
    public function batch(array $lifecycles, ?string $after, ?string $through = null): array
    {
        $query = TelegramAccount::withTrashed()->whereIn('lifecycle', $lifecycles)->where(function (Builder $due): void {
            $due->whereNull('next_reconcile_at')->orWhere('next_reconcile_at', '<=', now());
        })->where(function (Builder $unblocked): void {
            $unblocked->whereNull('reconcile_blocked_revision')->orWhereColumn('reconcile_blocked_revision', '!=', 'desired_revision');
        });
        if ($after !== null) {
            $query->where('id', '>', $after);
        }
        if ($through !== null) {
            $query->where('id', '<=', $through);
        }

        return $query->orderBy('id')->limit(25)->get()->map($this->data(...))->values()->all();
    }

    /** @param list<string> $lifecycles */
    public function deferredCount(array $lifecycles): int
    {
        return TelegramAccount::withTrashed()->whereIn('lifecycle', $lifecycles)->where(function (Builder $deferred): void {
            $deferred->where('next_reconcile_at', '>', now())->orWhereColumn('reconcile_blocked_revision', 'desired_revision');
        })->count();
    }

    public function blockedCount(): int
    {
        return TelegramAccount::query()->where('lifecycle', '!=', AccountLifecycle::Removed->value)->whereColumn('reconcile_blocked_revision', 'desired_revision')->count();
    }

    /** @return list<string> */
    public function unblock(?string $id): array
    {
        $query = TelegramAccount::query()->where('lifecycle', '!=', AccountLifecycle::Removed->value)->whereNotNull('reconcile_blocked_revision');
        if ($id !== null) {
            $query->whereKey($id);
        }
        $ids = array_values($query->pluck('id')->map(fn (mixed $value): string => Values::string($value))->all());
        if ($ids !== []) {
            TelegramAccount::query()->whereKey($ids)->update([
                'reconcile_blocked_revision' => null,
                'next_reconcile_at' => null,
                'reconcile_failures' => 0,
            ]);
        }

        return $ids;
    }

    /** @return array<int, array<string, mixed>> */
    public function desired(): array
    {
        return TelegramAccount::withTrashed()->where('lifecycle', '!=', 'removed')->orderBy('id')->get()->map(fn (TelegramAccount $account): array => $account->only(['id', 'storage_generation', 'lifecycle', 'desired_revision']))->values()->all();
    }

    public function attempted(AccountData $account): void
    {
        TelegramAccount::query()->whereKey($account->id)->where('desired_revision', $account->desired_revision)->update([
            'last_reconcile_attempt_at' => now(),
        ]);
    }

    public function succeeded(AccountData $account): void
    {
        TelegramAccount::query()->whereKey($account->id)->where('desired_revision', $account->desired_revision)->update([
            'last_reconcile_success_at' => now(),
            'next_reconcile_at' => null,
            'reconcile_failures' => 0,
            'reconcile_blocked_revision' => null,
        ]);
    }

    public function failed(AccountData $account, string $code, int $failures, ?int $delay): void
    {
        TelegramAccount::query()->whereKey($account->id)->where('desired_revision', $account->desired_revision)->update([
            'reconcile_failures' => $failures,
            'next_reconcile_at' => $delay === null ? null : now()->addSeconds($delay),
            'reconcile_blocked_revision' => $delay === null ? $account->desired_revision : null,
            'last_error_code' => $code,
        ]);
    }
}
