<?php

namespace App\Http\Requests;

use Illuminate\Validation\Rule;

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
            'proxy.id' => ['required_with:proxy', 'uuid'],
            'proxy.mode' => ['required_with:proxy', Rule::in(['direct', 'socks5', 'http', 'mtproto'])],
            'proxy.host' => ['nullable', 'string', 'max:255', 'required_if:proxy.mode,socks5,http,mtproto', 'prohibited_if:proxy.mode,direct'],
            'proxy.port' => ['nullable', 'integer', 'between:1,65535', 'required_if:proxy.mode,socks5,http,mtproto', 'prohibited_if:proxy.mode,direct'],
            'proxy.http_only' => ['boolean', 'prohibited_unless:proxy.mode,http'],
            'proxy.username' => ['nullable', 'string', 'max:255', 'prohibited_if:proxy.mode,direct,mtproto'],
            'proxy.password' => ['nullable', 'string', 'max:255', 'prohibited_if:proxy.mode,direct,mtproto'],
            'proxy.secret' => ['nullable', 'string', 'max:512', 'required_if:proxy.mode,mtproto', 'prohibited_unless:proxy.mode,mtproto'],
        ];
    }
}
