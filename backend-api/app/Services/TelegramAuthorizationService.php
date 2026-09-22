<?php

declare(strict_types=1);

namespace App\Services;

use App\Contracts\TdlibGateway;
use App\Data\Input;
use App\Support\Values;

final readonly class TelegramAuthorizationService
{
    public function __construct(private TelegramAccountService $accounts, private TdlibGateway $tdlib) {}

    /** @return array<string, mixed> */
    public function show(string $uuid, string $requestId): array
    {
        $this->accounts->find($uuid, false, $requestId);

        return Values::object($this->tdlib->snapshot($uuid, $requestId)['authorization'] ?? []);
    }

    /** @return array<string, mixed> */
    public function action(string $uuid, Input $input, string $requestId): array
    {
        return Values::object($this->accounts->authorizationAction($uuid, $input->all(), $requestId)['authorization'] ?? []);
    }
}
