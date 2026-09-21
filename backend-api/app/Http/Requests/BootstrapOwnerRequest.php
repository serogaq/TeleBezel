<?php

declare(strict_types=1);

namespace App\Http\Requests;

final class BootstrapOwnerRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'bootstrap_code' => ['required', 'string', 'max:200'],
            'password' => ['required', 'string', 'min:12', 'max:200'],
        ];
    }
}
