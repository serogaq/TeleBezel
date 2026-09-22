<?php

declare(strict_types=1);

namespace App\Services;

use App\Contracts\Repositories\AuthenticationRepository;
use App\Data\PrincipalContext;
use App\Exceptions\ApiException;

final readonly class AuthenticationService
{
    public function __construct(private AuthenticationRepository $principals) {}

    public function token(?string $token): PrincipalContext
    {
        if ($token === null || preg_match('/^tb_[A-Za-z0-9_-]{43}$/D', $token) !== 1) {
            throw new ApiException('auth.unauthorized', 401);
        }

        return $this->principals->token(hash('sha256', $token)) ?? throw new ApiException('auth.unauthorized', 401);
    }

    public function owner(?string $token): PrincipalContext
    {
        if ($token === null) {
            throw new ApiException('owner.authentication_required', 401);
        }

        $session = $this->principals->owner(hash('sha256', $token));
        if ($session !== null && ($session->expiresAt->isPast() || $session->authenticatedAt->addHours(12)->isPast() || $session->lastInteractiveAt->addMinutes(30)->isPast())) {
            $this->principals->revokeOwnerSession($session->id);
            $session = null;
        }
        if ($session === null) {
            throw new ApiException('owner.authentication_required', 401);
        }

        return new PrincipalContext('owner', $session->id, $session->instanceId);
    }
}
