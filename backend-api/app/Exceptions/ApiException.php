<?php

namespace App\Exceptions;

use RuntimeException;

final class ApiException extends RuntimeException
{
    public function __construct(public readonly string $errorCode, public readonly int $status, public readonly ?int $retryAfter = null)
    {
        parent::__construct($errorCode);
    }
}
