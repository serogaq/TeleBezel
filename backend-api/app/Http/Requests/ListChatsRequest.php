<?php

declare(strict_types=1);

namespace App\Http\Requests;

final class ListChatsRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'list' => ['sometimes', 'in:main,archive'],
            'limit' => ['sometimes', 'integer', 'between:1,50'],
            'cursor' => ['sometimes', 'string', 'max:4096'],
        ];
    }
}
