<?php

declare(strict_types=1);

namespace App\Services;

use App\Contracts\Repositories\DeviceRepository;
use App\Contracts\TdlibGateway;
use App\Data\Input;
use App\Data\PrincipalContext;
use App\Exceptions\ApiException;
use Illuminate\Support\Str;

final readonly class DeviceService
{
    public function __construct(private DeviceRepository $devices, private TdlibGateway $tdlib) {}

    private function deviceId(PrincipalContext $principal): string
    {
        if ($principal->type !== 'device') {
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
        return $this->devices->all($instanceId);
    }

    /** @return array{id: string, name: string, token: string} */
    public function create(string $instanceId, string $name): array
    {
        $token = 'tb_'.Str::random(43);

        return [
            ...$this->devices->create($instanceId, $name, hash('sha256', $token), substr($token, 0, 12)),
            'token' => $token,
        ];
    }

    public function revoke(string $instanceId, string $id, string $requestId): void
    {
        $this->devices->revoke($instanceId, $id);
        try {
            $this->tdlib->releasePrincipalInterests('device', $id, $requestId);
        } catch (ApiException) {
            /* Durable revocation wins; leases expire within 90 seconds. */
        }
    }
}
