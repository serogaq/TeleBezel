<?php

declare(strict_types=1);

namespace App\Repositories;

use App\Contracts\Repositories\ProxyProfileRepository as ProxyProfileRepositoryContract;
use App\Data\Input;
use App\Data\ProxyPolicyData;
use App\Data\ProxyProfileData;
use App\Models\Instance;
use App\Models\ProxyProfile;
use App\Models\TelegramAccount;
use App\Support\Values;
use Carbon\CarbonImmutable;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Str;

final class ProxyProfileRepository implements ProxyProfileRepositoryContract
{
    public function lockInstance(string $instanceId): void
    {
        Instance::query()->whereKey($instanceId)->lockForUpdate()->firstOrFail();
    }

    private function data(ProxyProfile $profile): ProxyProfileData
    {
        return new ProxyProfileData($profile->id, $profile->instance_id, $profile->label, $profile->mode, $profile->host, $profile->port, $profile->http_only, $profile->username, $profile->credentials ?? [], $profile->position, $profile->last_ping_ok, $profile->last_ping_ms, $profile->last_ping_error, $profile->last_ping_at === null ? null : CarbonImmutable::instance($profile->last_ping_at));
    }

    public function policy(?string $instanceId = null): ?ProxyPolicyData
    {
        $instance = $instanceId === null ? Instance::query()->first() : Instance::query()->findOrFail($instanceId);

        return $instance === null ? null : new ProxyPolicyData($instance->id, $instance->active_proxy_profile_id, $instance->configuration_revision, $instance->proxy_failure_action, $instance->proxy_connect_timeout_seconds, $instance->proxy_failure_started_at === null ? null : CarbonImmutable::instance($instance->proxy_failure_started_at));
    }

    /** @return array<int, ProxyProfileData> */
    public function all(string $instanceId): array
    {
        return ProxyProfile::query()->where('instance_id', $instanceId)->orderBy('position')->get()->map($this->data(...))->values()->all();
    }

    public function find(string $instanceId, string $id): ProxyProfileData
    {
        return $this->data(ProxyProfile::query()->whereKey($id)->where('instance_id', $instanceId)->firstOrFail());
    }

    public function create(string $instanceId, Input $input): ProxyProfileData
    {
        return DB::transaction(function () use ($instanceId, $input): ProxyProfileData {
            Instance::query()->whereKey($instanceId)->lockForUpdate()->firstOrFail();
            $data = $input->all();
            $credentials = array_filter([
                'password' => $input->nullableString('password'),
                'secret' => $input->nullableString('secret'),
            ], fn (?string $value): bool => $value !== null && $value !== '');
            $max = ProxyProfile::query()->where('instance_id', $instanceId)->max('position');

            return $this->data(ProxyProfile::query()->create([
                'id' => (string) Str::uuid(),
                'instance_id' => $instanceId,
                'label' => $input->string('label'),
                'mode' => $input->string('mode'),
                'host' => $input->string('host'),
                'port' => $input->integer('port'),
                'http_only' => $data['http_only'] ?? false,
                'username' => $input->nullableString('username'),
                'credentials' => $credentials === [] ? null : $credentials,
                'position' => $max === null ? 0 : Values::integer($max) + 1,
            ]));
        }, 3);
    }

    public function recordPing(ProxyProfileData $profile, bool $ok, ?int $latency, ?string $error): ProxyProfileData
    {
        ProxyProfile::query()->whereKey($profile->id)->update([
            'last_ping_ok' => $ok,
            'last_ping_ms' => $latency,
            'last_ping_error' => $error,
            'last_ping_at' => now(),
        ]);

        return $this->find($profile->instanceId, $profile->id);
    }

    public function runtimeAccount(): ?string
    {
        return TelegramAccount::query()->where('lifecycle', 'active')->orderBy('created_at')->first()?->id;
    }

    /** @return array<int, string> */
    public function inheritedAccounts(): array
    {
        return TelegramAccount::query()->whereNull('proxy_id')->where('lifecycle', 'active')->get()->map(fn (TelegramAccount $account): string => $account->id)->values()->all();
    }

    public function delete(string $instanceId, string $id): void
    {
        DB::transaction(function () use ($instanceId, $id): void {
            $this->lockInstance($instanceId);
            ProxyProfile::query()->whereKey($id)->where('instance_id', $instanceId)->firstOrFail()->delete();
        }, 3);
    }

    /** @return array<int, string> */
    public function activate(string $instanceId, ?string $id): array
    {
        return DB::transaction(function () use ($instanceId, $id): array {
            $instance = Instance::query()->whereKey($instanceId)->lockForUpdate()->firstOrFail();

            return $this->activateLocked($instance, $id);
        }, 3);
    }

    public function activateObserved(ProxyPolicyData $policy, ?string $id): ?array
    {
        return DB::transaction(function () use ($policy, $id): ?array {
            $instance = Instance::query()->whereKey($policy->instanceId)->lockForUpdate()->firstOrFail();
            if ($instance->configuration_revision !== $policy->revision || $instance->active_proxy_profile_id !== $policy->activeId || $instance->proxy_failure_action !== $policy->failureAction) {
                return null;
            }

            return $this->activateLocked($instance, $id);
        }, 3);
    }

    /** @return array<int, string> */
    private function activateLocked(Instance $instance, ?string $id): array
    {
        if ($id !== null) {
            $this->find($instance->id, $id);
        }
        $instance->forceFill([
            'active_proxy_profile_id' => $id,
            'proxy_activated_at' => now(),
            'proxy_failure_started_at' => null,
            'configuration_revision' => $instance->configuration_revision + 1,
        ])->save();
        $accounts = TelegramAccount::query()->whereNull('proxy_id')->where('lifecycle', '!=', 'removed')->lockForUpdate()->get();
        foreach ($accounts as $account) {
            $account->forceFill([
                'desired_revision' => $account->desired_revision + 1,
                'effective_config_id' => (string) Str::uuid(),
                'operation_id' => (string) Str::uuid(),
                'last_error_code' => null,
                'next_reconcile_at' => null,
                'reconcile_blocked_revision' => null,
            ])->save();
        }

        return $accounts->map(fn (TelegramAccount $account): string => $account->id)->values()->all();
    }

    public function configure(string $instanceId, Input $input): void
    {
        DB::transaction(function () use ($instanceId, $input): void {
            $instance = Instance::query()->whereKey($instanceId)->lockForUpdate()->firstOrFail();
            $instance->forceFill([
                'proxy_failure_action' => $input->string('failure_action'),
                'proxy_connect_timeout_seconds' => $input->integer('connect_timeout_seconds'),
                'configuration_revision' => $instance->configuration_revision + 1,
                'proxy_failure_started_at' => null,
            ])->save();
        }, 3);
    }

    public function recordFailure(ProxyPolicyData $policy, bool $failed): void
    {
        Instance::query()->whereKey($policy->instanceId)->where('configuration_revision', $policy->revision)->where('active_proxy_profile_id', $policy->activeId)->update([
            'proxy_failure_started_at' => $failed ? now() : null,
        ]);
    }
}
