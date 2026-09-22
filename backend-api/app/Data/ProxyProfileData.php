<?php

declare(strict_types=1);

namespace App\Data;

use Carbon\CarbonImmutable;

final readonly class ProxyProfileData
{
    /** @param array<string, mixed> $credentials */
    public function __construct(public string $id, public string $instanceId, public string $label, public string $mode, public string $host, public int $port, public bool $httpOnly, public ?string $username, public array $credentials, public int $position, public ?bool $pingOk, public ?int $pingMs, public ?string $pingError, public ?CarbonImmutable $pingAt) {}

    /** @return array<string, mixed> */
    public function definition(): array
    {
        return [
            'id' => $this->id,
            'mode' => $this->mode,
            'host' => $this->host,
            'port' => $this->port,
            'http_only' => $this->httpOnly,
            ...$this->username !== null && $this->username !== '' ? [
                'username' => $this->username,
            ] : [],
            ...$this->credentials,
        ];
    }

    /** @return array<string, mixed> */
    public function publicData(bool $active): array
    {
        return [
            'id' => $this->id,
            'label' => $this->label,
            'mode' => $this->mode,
            'host' => $this->host,
            'port' => $this->port,
            'http_only' => $this->httpOnly,
            'username' => $this->username,
            'has_credentials' => $this->credentials !== [],
            'position' => $this->position,
            'active' => $active,
            'ping' => [
                'ok' => $this->pingOk,
                'latency_ms' => $this->pingMs,
                'error' => $this->pingError,
                'tested_at' => $this->pingAt?->toISOString(),
            ],
        ];
    }
}
