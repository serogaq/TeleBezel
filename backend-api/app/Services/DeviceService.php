<?php

declare(strict_types=1);

namespace App\Services;

use App\Contracts\Repositories\AccessTokenRepository;
use App\Contracts\Repositories\DeviceRepository;
use App\Contracts\TransactionManager;
use App\Data\Input;
use App\Data\PrincipalContext;
use App\Enums\TokenType;
use App\Exceptions\ApiException;

final readonly class DeviceService
{
    public function __construct(private DeviceRepository $devices, private AccessTokenRepository $tokens, private AccessTokenService $issuer, private TransactionManager $transactions) {}

    private function deviceId(PrincipalContext $principal): string
    {
        if ($principal->type !== TokenType::Device->value) {
            throw new ApiException('auth.insufficient_scope', 403);
        }

        return $principal->id;
    }

    /** @return array<string, mixed> */
    public function preferences(PrincipalContext $principal): array
    {
        return $this->devices->preferences($this->deviceId($principal));
    }

    /** @return array<string, mixed> */
    public function update(PrincipalContext $principal, Input $input): array
    {
        $this->devices->update($this->deviceId($principal), $input);

        return $this->preferences($principal);
    }

    /** @return array<int, array<string, mixed>> */
    public function all(string $instanceId): array
    {
        return $this->tokens->listing($instanceId, TokenType::Device);
    }

    /** @return array{id: string, name: string, token: string} */
    public function create(string $instanceId, string $name): array
    {
        return $this->transactions->run(fn (): array => $this->issuer->issueDevice($instanceId, $name));
    }

    public function revoke(string $instanceId, string $id, string $requestId): void
    {
        if (! $this->issuer->revoke($id, $requestId, $instanceId, TokenType::Device)) {
            throw new ApiException('device.not_found', 404);
        }
    }
}
