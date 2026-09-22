<?php

declare(strict_types=1);

namespace App\Repositories;

use App\Contracts\Repositories\SettingsRepository as SettingsRepositoryContract;
use App\Exceptions\ApiException;
use App\Models\Instance;
use App\Models\TelegramAccount;
use App\Support\Values;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Str;

final class SettingsRepository implements SettingsRepositoryContract
{
    /** @return array<string, mixed> */
    public function show(string $instanceId): array
    {
        $instance = Instance::query()->findOrFail($instanceId);
        $scheduler = DB::table('scheduler_statuses')->where('name', 'reconciliation')->first();

        return [
            'instance_id' => $instance->id,
            'configuration_revision' => $instance->configuration_revision,
            'telegram' => [
                'api_id' => $instance->telegram_api_id,
                'has_api_hash' => $instance->telegram_api_hash !== null,
            ],
            'proxy_runtime' => [
                'active_profile_id' => $instance->active_proxy_profile_id,
                'failure_action' => $instance->proxy_failure_action,
                'connect_timeout_seconds' => $instance->proxy_connect_timeout_seconds,
            ],
            'scheduler' => $scheduler === null ? null : [
                'last_tick_at' => $scheduler->last_tick_at,
                'last_result' => $scheduler->last_result,
                'duration_ms' => $scheduler->duration_ms,
                'succeeded' => $scheduler->succeeded,
                'failed' => $scheduler->failed,
                'deferred' => $scheduler->deferred,
                'blocked_accounts' => TelegramAccount::query()->where('lifecycle', '!=', 'removed')->whereColumn('reconcile_blocked_revision', 'desired_revision')->count(),
            ],
        ];
    }

    /** @param array<string, mixed> $data
     * @return array<int, string> */
    public function update(string $instanceId, array $data): array
    {
        $affected = DB::transaction(function () use ($instanceId, $data): array {
            $instance = Instance::query()->whereKey($instanceId)->lockForUpdate()->firstOrFail();
            if ($instance->configuration_revision !== Values::integer($data['configuration_revision'])) {
                throw new ApiException('operation.conflict', 409);
            }
            if (array_key_exists('telegram_api_id', $data)) {
                $instance->telegram_api_id = $data['telegram_api_id'] === null ? null : max(1, Values::integer($data['telegram_api_id']));
            }
            if (array_key_exists('telegram_api_hash', $data)) {
                $instance->telegram_api_hash = $data['telegram_api_hash'] === null ? null : Values::string($data['telegram_api_hash']);
            }
            $instance->configuration_revision++;
            $instance->save();
            $affected = TelegramAccount::query()->where('lifecycle', '!=', 'removed')->lockForUpdate()->get();
            foreach ($affected as $account) {
                $account->desired_revision++;
                $account->effective_config_id = (string) Str::uuid();
                $account->operation_id = (string) Str::uuid();
                $account->last_error_code = null;
                $account->next_reconcile_at = null;
                $account->reconcile_blocked_revision = null;
                $account->save();
            }

            return $affected->all();
        }, 3);

        return array_map(fn (TelegramAccount $account): string => $account->id, $affected);
    }
}
