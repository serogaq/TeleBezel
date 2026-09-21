<?php

declare(strict_types=1);

namespace App\Http\Requests;

final class LoginOwnerRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'password' => ['required', 'string', 'max:200'],
        ];
    }
}
