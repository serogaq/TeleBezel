<?php

declare(strict_types=1);

namespace App\Http\Requests;

use App\Exceptions\ApiException;
use Illuminate\Support\Str;

final class InterestRequest extends ApiFormRequest
{
    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [];
    }

    protected function passedValidation(): void
    {
        if (! Str::isUuid($this->route('viewId'))) {
            throw new ApiException('request.invalid', 422);
        }
    }
}
