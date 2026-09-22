<?php

declare(strict_types=1);

namespace App\Http\Resources;

final class AuthorizationResource extends ApiResource
{
    /** @param array<string, mixed> $data */
    public function __construct(array $data)
    {
        parent::__construct(array_intersect_key($data, array_flip(['state', 'authorization_version', 'allowed_actions', 'resend_available', 'resend_after_seconds', 'delivery_method', 'qr_link'])));
    }
}
