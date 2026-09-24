<?php

declare(strict_types=1);

namespace App\Http\Requests;

final class ListUpdatesRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'cursor' => ['sometimes', 'string', 'max:4096'],
            'limit' => ['sometimes', 'integer', 'between:1,100'],
            'types' => ['sometimes', 'string', 'max:64', 'regex:/^(chat|message|send|connection)(,(chat|message|send|connection))*$/'],
            'wait' => ['sometimes', 'integer', 'in:0'],
        ];
    }
}
