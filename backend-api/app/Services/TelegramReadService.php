<?php

declare(strict_types=1);

namespace App\Services;

use App\Contracts\TdlibGateway;
use App\Data\Input;
use App\Data\PrincipalContext;
use App\Data\TelegramId;

final readonly class TelegramReadService
{
    public function __construct(private TelegramAccountService $accounts, private TdlibGateway $tdlib) {}

    /** @return array<string, mixed> */
    public function chats(string $uuid, Input $input, string $requestId): array
    {
        $this->accounts->find($uuid, false, $requestId);

        return $this->tdlib->chats($uuid, $input->all() + [
            'list' => 'main',
            'limit' => 20,
        ], $requestId);
    }

    /** @return array<string, mixed> */
    public function chat(string $uuid, string $chatId, Input $input, PrincipalContext $principal, string $requestId): array
    {
        $id = new TelegramId($chatId, 'chat.not_found');
        $this->accounts->find($uuid, false, $requestId);

        return $this->tdlib->chat($uuid, $id->value, $input->all() + $this->principal($principal), $requestId);
    }

    /** @return array<string, mixed> */
    public function messages(string $uuid, string $chatId, Input $input, PrincipalContext $principal, string $requestId): array
    {
        $id = new TelegramId($chatId, 'chat.not_found');
        $this->accounts->find($uuid, false, $requestId);
        $query = $input->all();
        if ($input->has('retry_cursor')) {
            $query['cursor'] = $input->string('retry_cursor');
            unset($query['retry_cursor']);
        }

        return $this->tdlib->messages($uuid, $id->value, $query + $this->principal($principal) + [
            'limit' => 30,
        ], $requestId);
    }

    /** @return array<string, mixed> */
    public function message(string $uuid, string $chatId, string $messageId, Input $input, PrincipalContext $principal, string $requestId): array
    {
        $chat = new TelegramId($chatId, 'chat.not_found');
        $message = new TelegramId($messageId, 'message.not_found');
        $this->accounts->find($uuid, false, $requestId);

        return $this->tdlib->message($uuid, $chat->value, $message->value, $input->all() + $this->principal($principal), $requestId);
    }

    /** @return array<string, mixed> */
    public function updates(string $uuid, Input $input, string $requestId): array
    {
        $this->accounts->find($uuid, false, $requestId);

        return $this->tdlib->updates($uuid, $input->all() + [
            'limit' => 100,
        ], $requestId);
    }

    /** @return array<string, mixed> */
    public function interest(bool $active, string $uuid, string $chatId, string $viewId, PrincipalContext $principal, string $requestId): array
    {
        $chat = new TelegramId($chatId, 'chat.not_found');
        $this->accounts->find($uuid, false, $requestId);

        return $this->tdlib->interest($active ? 'PUT' : 'DELETE', $uuid, $chat->value, $viewId, $this->principal($principal), $requestId);
    }

    /** @return array{principal_type: string, principal_id: string} */
    private function principal(PrincipalContext $principal): array
    {
        return [
            'principal_type' => $principal->type,
            'principal_id' => $principal->id,
        ];
    }
}
