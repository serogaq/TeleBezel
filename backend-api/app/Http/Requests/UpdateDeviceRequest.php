<?php

declare(strict_types=1);

namespace App\Http\Requests;

final class UpdateDeviceRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'name' => ['sometimes', 'string', 'max:100'],
            'locale' => ['sometimes', 'in:auto,ru,en'],
            'default_account_id' => ['sometimes', 'nullable', 'uuid'],
            'chat_list' => ['sometimes', 'in:main,archive'],
        ];
    }
}
