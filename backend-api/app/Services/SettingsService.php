<?php

declare(strict_types=1);

namespace App\Services;

use App\Contracts\Repositories\SettingsRepository;
use App\Data\Input;
use App\Exceptions\ApiException;

final readonly class SettingsService
{
    public function __construct(private SettingsRepository $settings, private TelegramAccountService $accounts) {}

    /** @return array<string, mixed> */
    public function show(string $instanceId): array
    {
        return $this->settings->show($instanceId);
    }

    /** @return array<string, mixed> */
    public function update(string $instanceId, Input $input, string $requestId): array
    {
        foreach ($this->settings->update($instanceId, $input->all()) as $id) {
            try {
                $this->accounts->reconcile($this->accounts->find($id, false, $requestId), $requestId);
            } catch (ApiException) {
                // Configuration is durable; the scheduler retries each account independently.
            }
        }

        return $this->show($instanceId);
    }
}
