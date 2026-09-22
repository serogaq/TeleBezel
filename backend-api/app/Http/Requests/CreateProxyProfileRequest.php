<?php

declare(strict_types=1);

namespace App\Http\Requests;

use Illuminate\Validation\Rule;

final class CreateProxyProfileRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'label' => ['required', 'string', 'max:100'],
            'mode' => ['required', Rule::in(['socks5', 'http', 'mtproto'])],
            'host' => ['required', 'string', 'max:255'],
            'port' => ['required', 'integer', 'between:1,65535'],
            'http_only' => ['sometimes', 'boolean'],
            'username' => ['nullable', 'string', 'max:255', 'prohibited_if:mode,mtproto'],
            'password' => ['nullable', 'string', 'max:255', 'prohibited_if:mode,mtproto'],
            'secret' => ['nullable', 'string', 'max:512', 'required_if:mode,mtproto', 'prohibited_unless:mode,mtproto'],
        ];
    }
}
