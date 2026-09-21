<?php

namespace App\Services;

use App\Enums\AccountLifecycle;
use App\Exceptions\ApiException;
use App\Models\Instance;
use App\Models\ProxyProfile;
use App\Models\TelegramAccount;
use Illuminate\Support\Facades\Cache;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Str;

final class ProxyProfileService
{
    public function __construct(private readonly TdlibGateway $tdlib, private readonly TelegramAccountService $accounts) {}

    /** @return array<int, array<string, mixed>> */
    public function all(string $instanceId): array
    {
        $active = Instance::query()->findOrFail($instanceId)->active_proxy_profile_id;

        return ProxyProfile::query()->where('instance_id', $instanceId)->orderBy('position')->get()
            ->map(fn (ProxyProfile $profile): array => $this->resource($profile, $profile->id === $active))->all();
    }

    /** @param array<string, mixed> $data
     * @return array<string, mixed>
     */
    public function create(string $instanceId, array $data, string $requestId): array
    {
        if (ProxyProfile::query()->where('instance_id', $instanceId)->count() >= 20) {
            throw new ApiException('proxy.limit_reached', 422);
        }
        $credentials = array_filter(['password' => $data['password'] ?? null, 'secret' => $data['secret'] ?? null], fn ($value) => $value !== null && $value !== '');
        $profile = ProxyProfile::query()->create(['id' => (string) Str::uuid(), 'instance_id' => $instanceId,
            'label' => $data['label'], 'mode' => $data['mode'], 'host' => $data['host'], 'port' => $data['port'],
            'http_only' => $data['http_only'] ?? false, 'username' => $data['username'] ?? null,
            'credentials' => $credentials === [] ? null : $credentials,
            'position' => ((int) ProxyProfile::query()->where('instance_id', $instanceId)->max('position')) + 1]);
        $this->ping($profile, $requestId);

        return $this->resource($profile->fresh(), false);
    }

    /** @return array<string, mixed> */
    public function ping(ProxyProfile $profile, string $requestId): array
    {
        $account = TelegramAccount::query()->where('lifecycle', AccountLifecycle::Active->value)->orderBy('created_at')->first();
        if ($account === null) {
            $profile->forceFill(['last_ping_ok' => false, 'last_ping_ms' => null,
                'last_ping_error' => 'proxy.no_runtime', 'last_ping_at' => now()])->save();
        } else {
            try {
                $result = $this->tdlib->pingProxy($account->id, $this->definition($profile), $requestId);
                $profile->forceFill(['last_ping_ok' => true, 'last_ping_ms' => (int) ($result['latency_ms'] ?? 0),
                    'last_ping_error' => null, 'last_ping_at' => now()])->save();
            } catch (ApiException $exception) {
                $profile->forceFill(['last_ping_ok' => false, 'last_ping_ms' => null,
                    'last_ping_error' => $exception->errorCode, 'last_ping_at' => now()])->save();
            }
        }

        return $this->resource($profile->fresh(), Instance::query()->findOrFail($profile->instance_id)->active_proxy_profile_id === $profile->id);
    }

    /** @return array<int, array<string, mixed>> */
    public function pingAll(string $instanceId, string $requestId): array
    {
        return ProxyProfile::query()->where('instance_id', $instanceId)->orderBy('position')->get()
            ->map(fn (ProxyProfile $profile): array => $this->ping($profile, $requestId))->all();
    }

    public function delete(ProxyProfile $profile): void
    {
        if (Instance::query()->whereKey($profile->instance_id)->where('active_proxy_profile_id', $profile->id)->exists()) {
            throw new ApiException('proxy.active', 409);
        }
        $profile->delete();
    }

    public function activate(string $instanceId, ?ProxyProfile $profile, string $requestId): void
    {
        $affected = DB::transaction(function () use ($instanceId, $profile): array {
            $instance = Instance::query()->whereKey($instanceId)->lockForUpdate()->firstOrFail();
            $instance->forceFill(['active_proxy_profile_id' => $profile?->id, 'proxy_activated_at' => now(),
                'proxy_failure_started_at' => null, 'configuration_revision' => $instance->configuration_revision + 1])->save();
            $accounts = TelegramAccount::query()->whereNull('proxy_id')->where('lifecycle', '!=', AccountLifecycle::Removed->value)->lockForUpdate()->get();
            foreach ($accounts as $account) {
                $account->forceFill(['desired_revision' => $account->desired_revision + 1,
                    'effective_config_id' => (string) Str::uuid(), 'operation_id' => (string) Str::uuid(),
                    'last_error_code' => null, 'next_reconcile_at' => null, 'reconcile_blocked_revision' => null])->save();
            }

            return $accounts->all();
        }, 3);
        foreach ($affected as $account) {
            $this->accounts->applyEffectiveProxy($account, $requestId);
        }
    }

    /** @param array<string, mixed> $data */
    public function configure(string $instanceId, array $data): void
    {
        Instance::query()->whereKey($instanceId)->update(['proxy_failure_action' => $data['failure_action'],
            'proxy_connect_timeout_seconds' => $data['connect_timeout_seconds']]);
    }

    public function monitor(string $requestId): void
    {
        $lock = Cache::lock('telebezel:proxy-monitor', 10);
        if (! $lock->get()) {
            return;
        }
        try {
            $instance = Instance::query()->first();
            if (! $instance instanceof Instance || $instance->active_proxy_profile_id === null) {
                return;
            }
            $accounts = TelegramAccount::query()->whereNull('proxy_id')->where('lifecycle', AccountLifecycle::Active->value)->get();
            if ($accounts->isEmpty()) {
                return;
            }
            try {
                $snapshots = $this->tdlib->listSnapshots($accounts->pluck('id')->all(), $requestId)['accounts'] ?? [];
            } catch (ApiException) {
                return;
            }
            $ready = $accounts->contains(fn (TelegramAccount $account): bool => ($snapshots[$account->id]['connection_state'] ?? null) === 'ready');
            if ($ready) {
                if ($instance->proxy_failure_started_at !== null) {
                    $instance->forceFill(['proxy_failure_started_at' => null])->save();
                }

                return;
            }
            if ($instance->proxy_failure_started_at === null) {
                $instance->forceFill(['proxy_failure_started_at' => now()])->save();

                return;
            }
            if ($instance->proxy_failure_started_at->addSeconds($instance->proxy_connect_timeout_seconds)->isFuture()) {
                return;
            }
            $next = null;
            if ($instance->proxy_failure_action === 'next') {
                $current = ProxyProfile::query()->find($instance->active_proxy_profile_id);
                $next = ProxyProfile::query()->where('instance_id', $instance->id)->where('position', '>', $current->position)->orderBy('position')->first()
                    ?? ProxyProfile::query()->where('instance_id', $instance->id)->where('id', '!=', $instance->active_proxy_profile_id)->orderBy('position')->first();
            }
            $this->activate($instance->id, $next, $requestId);
        } finally {
            $lock->release();
        }
    }

    /** @return array<string, mixed> */
    public function definition(ProxyProfile $profile): array
    {
        return ['id' => $profile->id, 'mode' => $profile->mode, 'host' => $profile->host, 'port' => $profile->port,
            'http_only' => $profile->http_only, ...($profile->username ? ['username' => $profile->username] : []),
            ...($profile->credentials ?? [])];
    }

    /** @return array<string, mixed> */
    private function resource(ProxyProfile $profile, bool $active): array
    {
        return ['id' => $profile->id, 'label' => $profile->label, 'mode' => $profile->mode, 'host' => $profile->host,
            'port' => $profile->port, 'http_only' => $profile->http_only, 'username' => $profile->username,
            'has_credentials' => ! empty($profile->credentials), 'position' => $profile->position, 'active' => $active,
            'ping' => ['ok' => $profile->last_ping_ok, 'latency_ms' => $profile->last_ping_ms,
                'error' => $profile->last_ping_error, 'tested_at' => $profile->last_ping_at?->toISOString()]];
    }
}
