<?php

declare(strict_types=1);

namespace App\Http\Requests;

final class WriteQuickReplyRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'text' => ['required', 'string', 'max:512'],
        ];
    }
}
