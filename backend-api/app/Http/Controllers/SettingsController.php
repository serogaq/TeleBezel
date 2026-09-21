<?php

namespace App\Http\Controllers;

use App\Exceptions\ApiException;
use App\Models\Device;
use App\Models\Instance;
use App\Models\TelegramAccount;
use App\Services\TdlibGateway;
use App\Services\TelegramAccountService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Str;
use Illuminate\View\View;

final class SettingsController extends Controller
{
    public function page(): View
    {
        return view('settings');
    }

    public function show(Request $request): JsonResponse
    {
        $instance = Instance::query()->findOrFail($request->attributes->get('instance_id'));

        $scheduler = DB::table('scheduler_statuses')->where('name', 'reconciliation')->first();

        return response()->json(['data' => ['instance_id' => $instance->id, 'configuration_revision' => $instance->configuration_revision,
            'telegram' => ['api_id' => $instance->telegram_api_id, 'has_api_hash' => $instance->telegram_api_hash !== null],
            'proxy_runtime' => ['active_profile_id' => $instance->active_proxy_profile_id,
                'failure_action' => $instance->proxy_failure_action,
                'connect_timeout_seconds' => $instance->proxy_connect_timeout_seconds],
            'scheduler' => $scheduler === null ? null : ['last_tick_at' => $scheduler->last_tick_at,
                'last_result' => $scheduler->last_result, 'duration_ms' => $scheduler->duration_ms,
                'succeeded' => $scheduler->succeeded, 'failed' => $scheduler->failed, 'deferred' => $scheduler->deferred]]],
            200, ['Cache-Control' => 'no-store']);
    }

    public function update(Request $request, TelegramAccountService $accounts): JsonResponse
    {
        $data = $request->validate(['configuration_revision' => ['required', 'integer', 'min:1'], 'telegram_api_id' => ['sometimes', 'nullable', 'integer', 'min:1'],
            'telegram_api_hash' => ['sometimes', 'nullable', 'string', 'max:128'], 'global_proxy' => ['prohibited']]);
        $affected = DB::transaction(function () use ($request, $data): array {
            $instance = Instance::query()->whereKey($request->attributes->get('instance_id'))->lockForUpdate()->firstOrFail();
            if ($instance->configuration_revision !== (int) $data['configuration_revision']) {
                throw new ApiException('operation.conflict', 409);
            }
            if (array_key_exists('telegram_api_id', $data)) {
                $instance->telegram_api_id = $data['telegram_api_id'];
            }
            if (array_key_exists('telegram_api_hash', $data)) {
                $instance->telegram_api_hash = $data['telegram_api_hash'];
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
        foreach ($affected as $account) {
            $accounts->reconcile($account, (string) $request->attributes->get('request_id'));
        }

        return $this->show($request);
    }

    public function devices(Request $request): JsonResponse
    {
        $devices = Device::query()->where('instance_id', $request->attributes->get('instance_id'))->orderBy('created_at')->get()
            ->map(fn (Device $device): array => $device->only(['id', 'name', 'locale', 'chat_list', 'last_seen_at', 'revoked_at']))->all();

        return response()->json(['data' => $devices], 200, ['Cache-Control' => 'no-store']);
    }

    public function storeDevice(Request $request): JsonResponse
    {
        $data = $request->validate(['name' => ['required', 'string', 'max:100']]);
        $instanceId = (string) $request->attributes->get('instance_id');
        $token = 'tb_'.Str::random(43);
        $device = DB::transaction(function () use ($data, $instanceId, $token): Device {
            Instance::query()->whereKey($instanceId)->lockForUpdate()->firstOrFail();
            if (Device::query()->where('instance_id', $instanceId)->whereNull('revoked_at')->count() >= 20) {
                throw new ApiException('devices.limit_reached', 422);
            }

            return Device::query()->create(['id' => (string) Str::uuid(), 'instance_id' => $instanceId,
                'name' => $data['name'], 'token_prefix' => substr($token, 0, 12),
                'token_hash' => hash('sha256', $token)]);
        }, 3);

        return response()->json(['data' => ['id' => $device->id, 'name' => $device->name, 'token' => $token]],
            201, ['Cache-Control' => 'no-store']);
    }

    public function revokeDevice(string $id, Request $request, TdlibGateway $tdlib): JsonResponse
    {
        $device = Device::query()->whereKey($id)->where('instance_id', $request->attributes->get('instance_id'))->firstOrFail();
        $device->forceFill(['revoked_at' => now()])->save();
        try {
            $tdlib->releasePrincipalInterests('device', $device->id, (string) $request->attributes->get('request_id'));
        } catch (ApiException) {
            // Access is revoked immediately; failed best-effort leases expire within 90 seconds.
        }

        return response()->json(['data' => ['revoked' => true]], 200, ['Cache-Control' => 'no-store']);
    }

    public function quickReplies(Request $request): JsonResponse
    {
        return response()->json(['data' => DB::table('quick_replies')->where('instance_id', $request->attributes->get('instance_id'))
            ->orderBy('position')->get(['id', 'text', 'position'])], 200, ['Cache-Control' => 'no-store']);
    }

    public function storeQuickReply(Request $request): JsonResponse
    {
        $data = $request->validate(['text' => ['required', 'string', 'max:512']]);
        $instanceId = (string) $request->attributes->get('instance_id');
        $count = DB::table('quick_replies')->where('instance_id', $instanceId)->count();
        if ($count >= 50) {
            throw new ApiException('quick_replies.limit_reached', 422);
        }
        $id = (string) Str::uuid();
        DB::table('quick_replies')->insert(['id' => $id, 'instance_id' => $instanceId, 'text' => $data['text'],
            'position' => $count, 'created_at' => now(), 'updated_at' => now()]);

        return response()->json(['data' => ['id' => $id, 'text' => $data['text'], 'position' => $count]], 201, ['Cache-Control' => 'no-store']);
    }

    public function reorderQuickReplies(Request $request): JsonResponse
    {
        $data = $request->validate(['ids' => ['required', 'array', 'max:50'], 'ids.*' => ['uuid', 'distinct']]);
        $instanceId = (string) $request->attributes->get('instance_id');
        DB::transaction(function () use ($data, $instanceId): void {
            $existing = DB::table('quick_replies')->where('instance_id', $instanceId)->lockForUpdate()->pluck('id')->all();
            $requested = $data['ids'];
            sort($existing);
            sort($requested);
            if ($existing !== $requested) {
                throw new ApiException('operation.conflict', 409);
            }
            foreach ($data['ids'] as $position => $id) {
                DB::table('quick_replies')->where('id', $id)->update(['position' => $position + 100, 'updated_at' => now()]);
            }
            foreach ($data['ids'] as $position => $id) {
                DB::table('quick_replies')->where('id', $id)->update(['position' => $position, 'updated_at' => now()]);
            }
        }, 3);

        return $this->quickReplies($request);
    }

    public function updateQuickReply(string $id, Request $request): JsonResponse
    {
        $data = $request->validate(['text' => ['required', 'string', 'max:512']]);
        $updated = DB::table('quick_replies')->where('id', $id)
            ->where('instance_id', $request->attributes->get('instance_id'))
            ->update(['text' => $data['text'], 'updated_at' => now()]);
        if ($updated === 0) {
            throw new ApiException('quick_replies.not_found', 404);
        }

        return $this->quickReplies($request);
    }

    public function destroyQuickReply(string $id, Request $request): JsonResponse
    {
        DB::table('quick_replies')->where('id', $id)->where('instance_id', $request->attributes->get('instance_id'))->delete();

        return response()->json(['data' => ['deleted' => true]], 200, ['Cache-Control' => 'no-store']);
    }
}
