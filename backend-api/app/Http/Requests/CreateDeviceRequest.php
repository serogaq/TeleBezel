<?php

declare(strict_types=1);

namespace App\Http\Requests;

final class CreateDeviceRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'name' => ['required', 'string', 'max:100'],
        ];
    }
}
