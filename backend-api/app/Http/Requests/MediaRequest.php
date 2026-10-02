<?php

declare(strict_types=1);

namespace App\Http\Requests;

// The watch states what it can hold; everything else is refused.
final class MediaRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'index' => ['sometimes', 'integer', 'between:0,9'],
            'width' => ['required', 'integer', 'between:16,260'],
            'height' => ['required', 'integer', 'between:16,260'],
            'shape' => ['required', 'string', 'in:rect,round'],
            'budget' => ['required', 'integer', 'between:2048,28672'],
            'formats' => ['required', 'string', 'regex:/\A(p4|p2|p1)(,(p4|p2|p1)){0,2}\z/'],
            'reveal' => ['sometimes', 'string', 'in:0,1'],
        ];
    }
}
