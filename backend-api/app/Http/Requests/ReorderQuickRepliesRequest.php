<?php

declare(strict_types=1);

namespace App\Http\Requests;

final class ReorderQuickRepliesRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'ids' => ['required', 'array', 'max:50'],
            'ids.*' => ['uuid', 'distinct'],
        ];
    }
}
