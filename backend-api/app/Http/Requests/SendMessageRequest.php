<?php

declare(strict_types=1);

namespace App\Http\Requests;

final class SendMessageRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'text' => ['required', 'string', 'max:16384'],
            'reply_to_message_id' => ['sometimes', 'nullable', 'string', 'regex:/^[1-9][0-9]{0,18}$/'],
        ];
    }
}
