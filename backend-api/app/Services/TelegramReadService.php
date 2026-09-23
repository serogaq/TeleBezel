<?php

declare(strict_types=1);

namespace App\Services;

use App\Contracts\Repositories\TelegramAccountRepository;
use App\Contracts\TdlibGateway;
use App\Data\AccountData;
use App\Data\Input;
use App\Data\PrincipalContext;
use App\Data\TelegramId;
use App\Enums\AccountLifecycle;
use App\Exceptions\ApiException;

final readonly class TelegramReadService
{
    public function __construct(private TelegramAccountService $accounts, private TelegramAccountRepository $repository, private TdlibGateway $tdlib) {}

    /** @return array<string, mixed> */
    public function chats(string $uuid, Input $input, string $requestId): array
    {
        $this->assertReadable($uuid, $requestId);

        return $this->tdlib->chats($uuid, $input->all() + [
            'list' => 'main',
            'limit' => 20,
        ], $requestId);
    }

    /** @return array<string, mixed> */
    public function chat(string $uuid, string $chatId, Input $input, PrincipalContext $principal, string $requestId): array
    {
        $id = new TelegramId($chatId, 'chat.not_found');
        $this->assertReadable($uuid, $requestId);

        return $this->tdlib->chat($uuid, $id->value, $input->all() + $this->principal($principal), $requestId);
    }

    /** @return array<string, mixed> */
    public function messages(string $uuid, string $chatId, Input $input, PrincipalContext $principal, string $requestId): array
    {
        $id = new TelegramId($chatId, 'chat.not_found');
        $this->assertReadable($uuid, $requestId);
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
        $this->assertReadable($uuid, $requestId);

        return $this->tdlib->message($uuid, $chat->value, $message->value, $input->all() + $this->principal($principal), $requestId);
    }

    /** @return array{mime_type: string, bytes: string} */
    public function preview(string $uuid, string $chatId, string $messageId, string $previewId, string $requestId): array
    {
        $chat = new TelegramId($chatId, 'chat.not_found');
        $message = new TelegramId($messageId, 'message.not_found');
        if (preg_match('/\A[0-9a-f]{64}\z/D', $previewId) !== 1) {
            throw new ApiException('message.cache_miss', 404);
        }
        $this->assertReadable($uuid, $requestId);
        $preview = $this->tdlib->preview($uuid, $chat->value, $message->value, $previewId, $requestId);
        $mime = $preview['mime_type'] ?? null;
        $encoded = $preview['bytes_base64'] ?? null;
        if (! in_array($mime, ['image/jpeg', 'image/png', 'image/webp'], true) || ! is_string($encoded) || strlen($encoded) > 700000) {
            throw new ApiException('service.tdlib_unavailable', 503);
        }
        $bytes = base64_decode($encoded, true);
        if ($bytes === false || strlen($bytes) > 512 * 1024) {
            throw new ApiException('service.tdlib_unavailable', 503);
        }

        return [
            'mime_type' => $mime,
            'bytes' => $bytes,
        ];
    }

    /** @return array<string, mixed> */
    public function updates(string $uuid, Input $input, string $requestId): array
    {
        $account = $this->assertReadable($uuid, $requestId);
        $updates = $this->tdlib->updates($uuid, $input->all() + [
            'limit' => 100,
        ], $requestId);
        $updates['status'] = [
            'connection' => is_string($updates['connection'] ?? null) ? $updates['connection'] : 'unknown',
            'proxy' => $this->repository->usesProxy($account),
        ];

        return $updates;
    }

    /** @return array<string, mixed> */
    public function interest(bool $active, string $uuid, string $chatId, string $viewId, PrincipalContext $principal, string $requestId): array
    {
        $chat = new TelegramId($chatId, 'chat.not_found');
        $this->assertReadable($uuid, $requestId);

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

    private function assertReadable(string $uuid, string $requestId): AccountData
    {
        $account = $this->accounts->find($uuid, false, $requestId);
        if ($account->lifecycle !== AccountLifecycle::Active) {
            throw new ApiException('authorization.invalid_state', 409);
        }

        return $account;
    }
}
