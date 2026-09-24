<?php

declare(strict_types=1);

namespace App\Services;

use App\Contracts\Repositories\AdministrationRepository;
use App\Contracts\Repositories\OwnerAccessRepository;
use App\Contracts\TransactionManager;
use App\Enums\TokenType;
use App\Exceptions\ApiException;
use Illuminate\Contracts\Encryption\DecryptException;
use Illuminate\Support\Str;

final readonly class AdministrationService
{
    public function __construct(private AdministrationRepository $repository, private OwnerAccessRepository $owners, private AccessTokenService $issuer, private TransactionManager $transactions) {}

    /** @return array{id: string, token: string} */
    public function issue(string $name, TokenType $type = TokenType::Maintenance): array
    {
        return $this->transactions->run(function () use ($name, $type): array {
            $owner = $this->owners->lockOwner();
            $instanceId = $owner->id ?? (string) Str::uuid();
            if ($owner === null) {
                $this->owners->createInstance($instanceId);
            }

            return $type === TokenType::Device ? $this->issuer->issueDevice($instanceId, $name) : $this->issuer->issue($instanceId, $type, $name);
        });
    }

    public function revoke(string $id): bool
    {
        return $this->issuer->revoke($id, (string) Str::uuid());
    }

    public function bootstrap(): string
    {
        $code = strtoupper(Str::random(8).'-'.Str::random(8));
        $this->transactions->run(function () use ($code): void {
            if ($this->owners->lockOwner()?->passwordHash !== null) {
                throw new ApiException('bootstrap.consumed', 409);
            }
            $this->repository->bootstrap(hash('sha256', $code));
        });

        return $code;
    }

    public function keyStatus(): string
    {
        return $this->transactions->run(function (): string {
            $owner = $this->owners->lockOwner();
            if ($owner === null || $owner->passwordHash === null) {
                return 'deferred';
            }

            if ($owner->appKeyCheck !== 'telebezel-app-key-v1:'.$owner->id) {
                throw new DecryptException('APP_KEY control data does not match.');
            }

            return 'valid';
        });
    }
}
