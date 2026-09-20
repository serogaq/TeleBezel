<?php

namespace App\Http\Requests;

use Illuminate\Validation\Rule;

final class AuthorizationActionRequest extends ApiFormRequest
{
    public function authorize(): bool
    {
        return true;
    }

    /** @return array<string, mixed> */
    public function rules(): array
    {
        return [
            'action' => ['required', Rule::in(['submit_phone_number', 'submit_code', 'submit_password', 'submit_email_address', 'submit_email_code', 'start_qr', 'resend_code'])],
            'authorization_version' => ['required', 'string', 'max:200'],
            'value' => ['nullable', 'string', 'max:512', 'required_unless:action,start_qr,resend_code'],
        ];
    }
}
