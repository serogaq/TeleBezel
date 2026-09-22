<?php

declare(strict_types=1);

namespace App\Repositories;

use App\Contracts\Repositories\DeviceRepository as DeviceRepositoryContract;
use App\Data\Input;
use App\Exceptions\ApiException;
use App\Models\Device;
use App\Models\Instance;
use App\Models\TelegramAccount;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Str;

final class DeviceRepository implements DeviceRepositoryContract
{
    /** @return array<string, mixed> */
    public function preferences(string $id): array
    {
        return Device::query()->findOrFail($id)->only(['id', 'name', 'locale', 'default_account_id', 'chat_list']);
    }

    /** @return array<int, array<string, mixed>> */
    public function all(string $instanceId): array
    {
        return Device::query()->where('instance_id', $instanceId)->orderBy('created_at')->get()->map(fn (Device $device): array => $device->only(['id', 'name', 'locale', 'chat_list', 'last_seen_at', 'revoked_at']))->values()->all();
    }

    public function update(string $id, Input $input): void
    {
        DB::transaction(function () use ($id, $input): void {
            if ($input->has('default_account_id') && $input->nullableString('default_account_id') !== null && ! TelegramAccount::query()->whereKey($input->string('default_account_id'))->where('lifecycle', '!=', 'removed')->exists()) {
                throw new ApiException('request.invalid', 422);
            }
            Device::query()->findOrFail($id)->fill($input->all())->save();
        }, 3);
    }

    /** @return array{id: string, name: string} */
    public function create(string $instanceId, string $name, string $tokenHash, string $prefix): array
    {
        return DB::transaction(function () use ($instanceId, $name, $tokenHash, $prefix): array {
            Instance::query()->whereKey($instanceId)->lockForUpdate()->firstOrFail();
            if (Device::query()->where('instance_id', $instanceId)->whereNull('revoked_at')->count() >= 20) {
                throw new ApiException('devices.limit_reached', 422);
            }
            $id = (string) Str::uuid();
            Device::query()->create([
                'id' => $id,
                'instance_id' => $instanceId,
                'name' => $name,
                'token_prefix' => $prefix,
                'token_hash' => $tokenHash,
            ]);

            return [
                'id' => $id,
                'name' => $name,
            ];
        }, 3);
    }

    public function revoke(string $instanceId, string $id): void
    {
        Device::query()->whereKey($id)->where('instance_id', $instanceId)->firstOrFail()->forceFill([
            'revoked_at' => now(),
        ])->save();
    }
}
