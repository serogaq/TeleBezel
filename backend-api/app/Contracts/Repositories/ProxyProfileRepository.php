<?php

declare(strict_types=1);

namespace App\Contracts\Repositories;

use App\Data\Input;
use App\Data\ProxyPolicyData;
use App\Data\ProxyProfileData;

interface ProxyProfileRepository
{
    public function lockInstance(string $instanceId): void;

    public function policy(?string $instanceId = null): ?ProxyPolicyData;

    /** @return array<int, ProxyProfileData> */
    public function all(string $instanceId): array;

    public function find(string $instanceId, string $id): ProxyProfileData;

    public function create(string $instanceId, Input $input): ProxyProfileData;

    public function recordPing(ProxyProfileData $profile, bool $ok, ?int $latency, ?string $error): ProxyProfileData;

    public function runtimeAccount(): ?string;

    /** @return array<int, string> */
    public function inheritedAccounts(): array;

    public function delete(string $instanceId, string $id): void;

    /** @return array<int, string> */
    public function activate(string $instanceId, ?string $id): array;

    /** @return array<int, string>|null Null when the observed policy has changed. */
    public function activateObserved(ProxyPolicyData $policy, ?string $id): ?array;

    public function configure(string $instanceId, Input $input): void;

    public function recordFailure(ProxyPolicyData $policy, bool $failed): void;
}
