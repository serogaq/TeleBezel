<?php

declare(strict_types=1);

namespace App\Http\Requests;

use Illuminate\Validation\Rule;

final class ConfigureProxyProfilesRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'failure_action' => ['required', Rule::in(['direct', 'next', 'stay'])],
            'connect_timeout_seconds' => ['required', 'integer', 'between:3,300'],
        ];
    }
}
