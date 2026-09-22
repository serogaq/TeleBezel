<?php

declare(strict_types=1);

namespace App\Http\Requests;

use Illuminate\Validation\Rule;

final class ListMessagesRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'view_id' => ['required', 'uuid'],
            'limit' => ['sometimes', 'integer', 'between:1,50'],
            'cursor' => ['sometimes', 'string', 'max:4096'],
            'retry_cursor' => [Rule::prohibitedIf($this->has('cursor')), 'sometimes', 'string', 'max:4096'],
        ];
    }
}
