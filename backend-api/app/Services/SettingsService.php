<?php

declare(strict_types=1);

namespace App\Services;

use App\Contracts\Repositories\SettingsRepository;
use App\Data\Input;

final readonly class SettingsService
{
    public function __construct(private SettingsRepository $settings) {}

    /** @return array<string, mixed> */
    public function show(string $instanceId): array
    {
        return $this->settings->show($instanceId);
    }

    /** @return array<string, mixed> */
    public function update(string $instanceId, Input $input, string $requestId): array
    {
        $this->settings->update($instanceId, $input->all());

        return $this->show($instanceId);
    }
}
