<?php

declare(strict_types=1);

namespace App\Data;

use App\Exceptions\ApiException;

final readonly class TelegramId
{
    public string $value;

    public function __construct(string $value, string $errorCode = 'request.invalid')
    {
        if (preg_match('/^-?[1-9][0-9]{0,18}$/D', $value) !== 1 || filter_var($value, FILTER_VALIDATE_INT) === false) {
            throw new ApiException($errorCode, $errorCode === 'request.invalid' ? 422 : 404);
        }
        $this->value = $value;
    }
}
