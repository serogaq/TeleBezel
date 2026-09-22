<?php

declare(strict_types=1);

namespace App\Http\Requests;

// A preview is addressed by its own reusable token. It carries no view_id
// because it neither opens nor extends an interest lease.
final class PreviewRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [];
    }
}
