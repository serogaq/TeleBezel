<?php

declare(strict_types=1);

namespace App\Services;

use App\Contracts\Repositories\AccessTokenRepository;
use App\Data\AccessTokenData;
use App\Data\PrincipalContext;
use App\Exceptions\ApiException;

final readonly class AuthenticationService
{
    public const string COOKIE = 'telebezel_session';

    public const string CSRF_HEADER = 'X-TeleBezel-CSRF';

    public function __construct(private AccessTokenRepository $tokens, private string $appKey) {}

    public function token(?string $token): PrincipalContext
    {
        if ($token === null || ! self::wellFormed($token)) {
            throw new ApiException('auth.unauthorized', 401);
        }
        $data = $this->tokens->find(hash('sha256', $token));
        if ($data === null || $data->claims === null) {
            throw new ApiException('auth.unauthorized', 401);
        }
        if (($data->expiresAt !== null && ! $data->expiresAt->isFuture()) || ($data->idleTimeoutSeconds !== null && ($data->lastActiveAt === null || ! $data->lastActiveAt->addSeconds($data->idleTimeoutSeconds)->isFuture()))) {
            $this->tokens->revoke($data->id);
            throw new ApiException('auth.unauthorized', 401);
        }
        $this->tokens->touch($data);

        return $this->principal($data);
    }

    public function cookie(?string $token, ?string $csrf, bool $safeMethod): PrincipalContext
    {
        if ($token === null || ! self::wellFormed($token)) {
            throw new ApiException('auth.unauthorized', 401);
        }
        if (! $safeMethod && ($csrf === null || ! hash_equals($this->csrf($token), $csrf))) {
            throw new ApiException('auth.csrf_mismatch', 419);
        }

        return $this->token($token);
    }

    public function csrf(string $token): string
    {
        return hash_hmac('sha256', 'telebezel-csrf-v1|'.hash('sha256', $token), $this->appKey);
    }

    public static function wellFormed(string $token): bool
    {
        return preg_match('/^tb_[A-Za-z0-9_-]{43}$/D', $token) === 1;
    }

    private function principal(AccessTokenData $data): PrincipalContext
    {
        $claims = $data->claims ?? throw new ApiException('auth.unauthorized', 401);

        return new PrincipalContext($data->type->value, $data->id, $data->instanceId, $claims->permissions, $claims->accounts);
    }
}
