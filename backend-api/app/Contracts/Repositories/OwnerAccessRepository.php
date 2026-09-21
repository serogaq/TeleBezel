<?php

declare(strict_types=1);

namespace App\Contracts\Repositories;

use App\Data\BootstrapCodeData;
use App\Data\OwnerCredentials;
use Carbon\CarbonImmutable;

interface OwnerAccessRepository
{
    public function lockBootstrap(string $codeHash): ?BootstrapCodeData;

    public function lockOwner(): ?OwnerCredentials;

    public function createInstance(string $id): void;

    public function saveCredentials(string $instanceId, string $passwordHash, string $recoveryHash): void;

    public function consumeBootstrap(string $id): void;

    public function createSession(string $instanceId, string $tokenHash, CarbonImmutable $expiresAt): void;

    public function revokeAccess(string $instanceId): void;

    public function saveRecoveryHash(string $instanceId, string $hash): void;

    public function activity(string $id): void;

    public function logout(string $id): void;
}
