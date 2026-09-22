<?php

namespace App\Http\Requests;

final class UpdateProxyRequest extends ApiFormRequest
{
    public function authorize(): bool
    {
        return true;
    }

    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'desired_revision' => ['required', 'integer', 'min:1'],
            ...TelegramProxyRules::forPrefix(),
        ];
    }
}
