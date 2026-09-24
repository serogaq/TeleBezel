<?php

declare(strict_types=1);

namespace App\Services;

use App\Contracts\Repositories\AccessTokenRepository;
use App\Contracts\TdlibGateway;
use App\Data\TokenClaims;
use App\Enums\TokenType;
use App\Exceptions\ApiException;
use Carbon\CarbonImmutable;

final readonly class AccessTokenService
{
    public const int WEB_LIFETIME_HOURS = 12;

    public const int WEB_IDLE_SECONDS = 1800;

    public const int DEVICE_LIMIT = 20;

    public function __construct(private AccessTokenRepository $tokens, private TdlibGateway $tdlib) {}

    /** @return array{id: string, token: string} */
    public function issue(string $instanceId, TokenType $type, string $name, ?TokenClaims $claims = null, ?CarbonImmutable $expiresAt = null, ?int $idleTimeoutSeconds = null): array
    {
        $token = 'tb_'.rtrim(strtr(base64_encode(random_bytes(32)), '+/', '-_'), '=');
        $id = $this->tokens->issue($instanceId, $type, $name, hash('sha256', $token), substr($token, 0, 12), $claims ?? TokenClaims::full($type), $expiresAt, $idleTimeoutSeconds);

        return [
            'id' => $id,
            'token' => $token,
        ];
    }

    /** @return array{id: string, token: string} */
    public function issueWebSession(string $instanceId): array
    {
        return $this->issue($instanceId, TokenType::Maintenance, 'Web settings', expiresAt: CarbonImmutable::now()->addHours(self::WEB_LIFETIME_HOURS), idleTimeoutSeconds: self::WEB_IDLE_SECONDS);
    }

    /** @return array{id: string, name: string, token: string} */
    public function issueDevice(string $instanceId, string $name): array
    {
        $this->tokens->lockInstance($instanceId);
        if ($this->tokens->activeCount($instanceId, TokenType::Device) >= self::DEVICE_LIMIT) {
            throw new ApiException('devices.limit_reached', 422);
        }

        return [
            ...$this->issue($instanceId, TokenType::Device, $name),
            'name' => $name,
        ];
    }

    public function revoke(string $id, string $requestId, ?string $instanceId = null, ?TokenType $type = null): bool
    {
        $revoked = $this->tokens->revoke($id, $instanceId, $type);
        if ($revoked === null) {
            return false;
        }
        try {
            $this->tdlib->releasePrincipalInterests($revoked->value, $id, $requestId);
        } catch (ApiException) {
            /* Durable revocation wins; leases expire within 90 seconds. */
        }

        return true;
    }
}
