<?php

namespace App\Http\Requests;

final class DeleteTelegramAccountRequest extends ApiFormRequest
{
    public function authorize(): bool
    {
        return true;
    }

    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'acknowledge_remote_session_remains' => ['required', 'accepted'],
        ];
    }
}
