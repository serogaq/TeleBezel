<?php

namespace App\Http\Requests;

final class CreateTelegramAccountRequest extends ApiFormRequest
{
    public function authorize(): bool
    {
        return true;
    }

    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'label' => ['required', 'string', 'max:100'],
            'proxy' => ['sometimes', 'array:id,mode,host,port,http_only,username,password,secret'],
            ...($this->has('proxy') ? TelegramProxyRules::forPrefix('proxy.') : []),
        ];
    }
}
