<?php

namespace App\Http\Requests;

use Illuminate\Validation\Rule;

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
            'id' => ['nullable', 'uuid', 'required_unless:mode,inherit', 'prohibited_if:mode,inherit'],
            'mode' => ['required', Rule::in(['inherit', 'direct', 'socks5', 'http', 'mtproto'])],
            'host' => ['nullable', 'string', 'max:255', 'required_if:mode,socks5,http,mtproto', 'prohibited_if:mode,inherit,direct'],
            'port' => ['nullable', 'integer', 'between:1,65535', 'required_if:mode,socks5,http,mtproto', 'prohibited_if:mode,inherit,direct'],
            'http_only' => ['boolean', 'prohibited_unless:mode,http'],
            'username' => ['nullable', 'string', 'max:255', 'prohibited_if:mode,inherit,direct,mtproto'],
            'password' => ['nullable', 'string', 'max:255', 'prohibited_if:mode,inherit,direct,mtproto'],
            'secret' => ['nullable', 'string', 'max:512', 'required_if:mode,mtproto', 'prohibited_unless:mode,mtproto'],
        ];
    }
}
