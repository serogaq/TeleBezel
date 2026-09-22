<?php

namespace App\Http\Requests;

use App\Data\Input;
use App\Exceptions\ApiException;
use App\Support\Values;
use Illuminate\Contracts\Validation\Validator;
use Illuminate\Foundation\Http\FormRequest;

abstract class ApiFormRequest extends FormRequest
{
    public function authorize(): bool
    {
        return true;
    }

    public function inputData(): Input
    {
        return new Input(Values::object($this->validated()));
    }

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
        throw new ApiException('request.invalid', 422);
    }
}
