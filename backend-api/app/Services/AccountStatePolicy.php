<?php

declare(strict_types=1);

namespace App\Services;

use App\Data\AccountData;
use App\Enums\AccountLifecycle;
use App\Exceptions\ApiException;
use App\Support\Values;
use Carbon\CarbonImmutable;
use Illuminate\Support\Str;

final class AccountStatePolicy
{
    private function writable(AccountData $account): void
    {
        if ($account->lifecycle === AccountLifecycle::Removed) {
            throw new ApiException('account.gone', 410);
        }
        if ($account->lifecycle === AccountLifecycle::Removing) {
            throw new ApiException('operation.conflict', 409);
        }
    }

    /** @return array<string, mixed> */
    public function logout(AccountData $account): array
    {
        $this->writable($account);
        if ($account->lifecycle === AccountLifecycle::LogoutPending) {
            return [];
        }
        $operation = (string) Str::uuid();

        return [
            'lifecycle' => AccountLifecycle::LogoutPending,
            'desired_revision' => $account->desired_revision + 1,
            'authorization_generation' => $account->authorization_generation + 1,
            'logout_operation_id' => $operation,
            'operation_id' => $operation,
            'logout_completed_at' => null,
        ];
    }

    /** @return array<string, mixed> */
    public function remove(AccountData $account): array
    {
        if (in_array($account->lifecycle, [AccountLifecycle::Removing, AccountLifecycle::Removed], true)) {
            return [];
        }

        return [
            'lifecycle' => AccountLifecycle::Removing,
            'desired_revision' => $account->desired_revision + 1,
            'operation_id' => (string) Str::uuid(),
        ];
    }

    /** @param array<string, mixed> $proxy
     * @return array<string, mixed> */
    public function updateProxy(AccountData $account, int $revision, array $proxy): array
    {
        $this->writable($account);
        if ($account->lifecycle === AccountLifecycle::LogoutPending || $account->desired_revision !== $revision) {
            throw new ApiException('operation.conflict', 409);
        }

        return [
            'proxy_id' => $proxy['id'] ?? null,
            'proxy_server' => $proxy['host'] ?? null,
            'proxy_port' => $proxy['port'] ?? null,
            'proxy_type' => $proxy['mode'] === 'inherit' ? null : $proxy['mode'],
            'desired_revision' => $account->desired_revision + 1,
            'effective_config_id' => (string) Str::uuid(),
            'operation_id' => (string) Str::uuid(),
            'last_error_code' => null,
        ];
    }

    /** @param array<string, mixed> $snapshot
     * @return array<string, mixed> */
    public function snapshot(AccountData $account, array $snapshot): array
    {
        $projected = clone $account;
        $this->applySnapshot($projected, $snapshot, false);

        return [
            'applied_revision' => $projected->applied_revision,
            'operation_id' => $projected->operation_id,
            'authorization_generation' => $projected->authorization_generation,
            'runtime_available' => $projected->runtime_available,
            'authorization_state' => $projected->authorization_state,
            'connection_state' => $projected->connection_state,
            'last_error_code' => $projected->last_error_code,
        ];
    }

    /** @param array<string, mixed> $data
     * @return array<string, mixed> */
    public function confirm(AccountData $current, AccountData $expected, array $data): array
    {
        if ($current->lifecycle === AccountLifecycle::Removed || $current->desired_revision !== $expected->desired_revision || $current->storage_generation !== $expected->storage_generation || $current->effective_config_id !== $expected->effective_config_id || (isset($data['effective_config_id']) && $data['effective_config_id'] !== $current->effective_config_id)) {
            return [];
        }
        $changes = $this->snapshot($current, $data);
        $applied = $data['applied_revision'] ?? null;
        if (is_int($applied) && $applied === $expected->desired_revision) {
            $changes['applied_revision'] = $applied;
        }
        $completed = ($data['completed'] ?? false) === true;
        if ($completed && $current->lifecycle === AccountLifecycle::Removing) {
            return [
                'label' => null,
                'proxy_id' => null,
                'proxy_server' => null,
                'proxy_port' => null,
                'proxy_type' => null,
                'lifecycle' => AccountLifecycle::Removed,
                'runtime_available' => false,
                'authorization_state' => null,
                'connection_state' => null,
                'last_error_code' => null,
                'operation_id' => null,
                'applied_revision' => $changes['applied_revision'] ?? $current->applied_revision,
            ];
        }
        if ($completed && $current->lifecycle === AccountLifecycle::LogoutPending) {
            $changes += [
                'lifecycle' => AccountLifecycle::Active,
                'logout_completed_at' => CarbonImmutable::now(),
            ];
            $changes['operation_id'] = null;
        } elseif ($current->lifecycle === AccountLifecycle::Provisioning && ($changes['applied_revision'] ?? $current->applied_revision) === $current->desired_revision) {
            $changes['lifecycle'] = AccountLifecycle::Active;
            $changes['operation_id'] = null;
        } elseif ($completed && $current->lifecycle === AccountLifecycle::Active) {
            $changes['operation_id'] = null;
        }

        return $changes;
    }

    /** @param array<string, mixed> $snapshot */
    public function applySnapshot(AccountData $account, array $snapshot, bool $includeIdentity = true): void
    {
        if (isset($snapshot['generation']) && $snapshot['generation'] !== $account->storage_generation) {
            return;
        }
        $authorizationGeneration = $snapshot['authorization_generation'] ?? null;
        if (is_int($authorizationGeneration) && $authorizationGeneration > $account->authorization_generation) {
            $account->authorization_generation = $authorizationGeneration;
        }
        $applied = $snapshot['applied_revision'] ?? null;
        $effectiveConfigId = $snapshot['effective_config_id'] ?? null;
        if (is_int($applied) && $applied === $account->desired_revision && is_string($effectiveConfigId) && hash_equals((string) $account->effective_config_id, $effectiveConfigId)) {
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
            $account->telegram_identity = Values::object($snapshot['telegram_identity']);
        }
    }
}
