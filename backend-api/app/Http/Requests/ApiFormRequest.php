<?php

namespace App\Http\Requests;

use Illuminate\Contracts\Validation\Validator;
use Illuminate\Foundation\Http\FormRequest;
use Illuminate\Http\Exceptions\HttpResponseException;

abstract class ApiFormRequest extends FormRequest
{
    /** @return array<string, mixed> */
    abstract public function rules(): array;

    public function withValidator(Validator $validator): void
    {
        $validator->after(function (Validator $validator): void {
            $allowed = [];
            foreach (array_keys($this->rules()) as $field) {
                $allowed[] = explode('.', $field, 2)[0];
            }
            foreach (array_keys($this->all()) as $field) {
                if (! in_array($field, $allowed, true)) {
                    $validator->errors()->add($field, 'Unknown field.');
                }
            }
        });
    }

    protected function failedValidation(Validator $validator): void
    {
        throw new HttpResponseException(response()->json([
            'error' => ['code' => 'request.invalid'],
            'request_id' => $this->attributes->get('request_id'),
        ], 422, ['Cache-Control' => 'no-store']));
    }
}
