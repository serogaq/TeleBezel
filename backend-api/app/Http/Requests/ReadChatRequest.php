<?php

declare(strict_types=1);

namespace App\Http\Requests;

final class ReadChatRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'view_id' => ['required', 'uuid'],
        ];
    }
}
