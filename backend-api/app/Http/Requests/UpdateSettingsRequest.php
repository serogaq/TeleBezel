<?php

declare(strict_types=1);

namespace App\Http\Requests;

final class UpdateSettingsRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'configuration_revision' => ['required', 'integer', 'min:1'],
            'telegram_api_id' => ['sometimes', 'nullable', 'integer', 'min:1'],
            'telegram_api_hash' => ['sometimes', 'nullable', 'string', 'max:128'],
            'global_proxy' => ['prohibited'],
        ];
    }
}
