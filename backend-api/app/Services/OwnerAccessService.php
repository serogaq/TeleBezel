<?php

declare(strict_types=1);

namespace App\Services;

use App\Contracts\Repositories\AccessTokenRepository;
use App\Contracts\Repositories\OwnerAccessRepository;
use App\Contracts\TransactionManager;
use App\Exceptions\ApiException;
use Illuminate\Contracts\Hashing\Hasher;
use Illuminate\Support\Str;

final readonly class OwnerAccessService
{
    public function __construct(private OwnerAccessRepository $access, private AccessTokenRepository $tokens, private AccessTokenService $issuer, private TransactionManager $transactions, private Hasher $hasher) {}

    /** @return array{string, string, string} */
    public function bootstrap(string $code, string $password): array
    {
        return $this->transactions->run(function () use ($code, $password): array {
            $owner = $this->access->lockOwner();
            $entry = $this->access->lockBootstrap(hash('sha256', $code));
            if ($entry === null || $entry->consumed || $entry->expiresAt->isPast()) {
                throw new ApiException('bootstrap.invalid', 422);
            }
            if ($owner?->passwordHash !== null) {
                throw new ApiException('bootstrap.consumed', 409);
            }
            $id = $owner->id ?? (string) Str::uuid();
            if ($owner === null) {
                $this->access->createInstance($id);
            }
            $recovery = $this->newRecoveryCode();
            $this->access->saveCredentials($id, $this->hasher->make($password), $this->hasher->make($recovery));
            $this->access->consumeBootstrap($entry->id);

            return [$id, $this->newSession($id), $recovery];
        });
    }

    /** @return array{string, string} */
    public function login(string $password): array
    {
        return $this->transactions->run(function () use ($password): array {
            $owner = $this->access->lockOwner();
            if ($owner === null || $owner->passwordHash === null || ! $this->hasher->check($password, $owner->passwordHash)) {
                throw new ApiException('owner.invalid_credentials', 422);
            }

            return [$owner->id, $this->newSession($owner->id)];
        });
    }

    /** @return array{string, string} */
    public function recover(string $code, string $password): array
    {
        return $this->transactions->run(function () use ($code, $password): array {
            $owner = $this->access->lockOwner();
            if ($owner === null || $owner->recoveryHash === null || ! $this->hasher->check($code, $owner->recoveryHash)) {
                throw new ApiException('owner.invalid_recovery_code', 422);
            }
            $recovery = $this->newRecoveryCode();
            $this->access->saveCredentials($owner->id, $this->hasher->make($password), $this->hasher->make($recovery));
            $this->tokens->revokeInstance($owner->id);

            return [$this->newSession($owner->id), $recovery];
        });
    }

    public function rotateRecoveryCode(string $instanceId): string
    {
        return $this->transactions->run(function () use ($instanceId): string {
            $owner = $this->access->lockOwner();
            if ($owner === null || $owner->id !== $instanceId) {
                throw new ApiException('owner.authentication_required', 401);
            }
            $recovery = $this->newRecoveryCode();
            $this->access->saveRecoveryHash($instanceId, $this->hasher->make($recovery));

            return $recovery;
        });
    }

    public function activity(string $tokenId): void
    {
        $this->tokens->activity($tokenId);
    }

    public function logout(string $tokenId, string $requestId): void
    {
        $this->issuer->revoke($tokenId, $requestId);
    }

    private function newSession(string $instanceId): string
    {
        return $this->issuer->issueWebSession($instanceId)['token'];
    }

    private function newRecoveryCode(): string
    {
        return implode('-', str_split(strtoupper(Str::random(24)), 6));
    }
}
