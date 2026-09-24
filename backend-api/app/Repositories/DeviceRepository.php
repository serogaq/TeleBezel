<?php

declare(strict_types=1);

namespace App\Repositories;

use App\Contracts\Repositories\DeviceRepository as DeviceRepositoryContract;
use App\Data\Input;
use App\Exceptions\ApiException;
use App\Models\AccessToken;
use App\Models\DeviceProfile;
use App\Models\TelegramAccount;
use Illuminate\Support\Facades\DB;

final class DeviceRepository implements DeviceRepositoryContract
{
    /** @return array<string, mixed> */
    public function preferences(string $tokenId): array
    {
        $token = AccessToken::query()->findOrFail($tokenId);
        $profile = DeviceProfile::query()->firstOrCreate([
            'token_id' => $tokenId,
        ]);

        return [
            'id' => $token->id,
            'name' => $token->name,
            'locale' => $profile->getAttribute('locale') ?? 'auto',
            'default_account_id' => $profile->getAttribute('default_account_id'),
            'chat_list' => $profile->getAttribute('chat_list') ?? 'main',
        ];
    }

    public function update(string $tokenId, Input $input): void
    {
        DB::transaction(function () use ($tokenId, $input): void {
            if ($input->has('default_account_id') && $input->nullableString('default_account_id') !== null && ! TelegramAccount::query()->whereKey($input->string('default_account_id'))->where('lifecycle', '!=', 'removed')->exists()) {
                throw new ApiException('request.invalid', 422);
            }
            $values = $input->all();
            if (array_key_exists('name', $values)) {
                AccessToken::query()->findOrFail($tokenId)->forceFill([
                    'name' => $values['name'],
                ])->save();
                unset($values['name']);
            }
            DeviceProfile::query()->firstOrCreate([
                'token_id' => $tokenId,
            ])->fill($values)->save();
        }, 3);
    }
}
