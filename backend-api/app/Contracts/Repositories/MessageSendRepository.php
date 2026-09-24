<?php

declare(strict_types=1);

namespace App\Contracts\Repositories;

use App\Data\MessageSendData;

interface MessageSendRepository
{
    /** @param array{instance_id: string, token_id: string, account_id: string, storage_generation: string, authorization_generation: int, chat_id: string, reply_to_message_id: ?string, key_hash: string, request_hmac: string} $attributes
     * @return array{MessageSendData, bool} */
    public function claim(array $attributes): array;

    public function find(string $id): ?MessageSendData;

    public function owned(string $tokenId, string $accountId, string $id): ?MessageSendData;

    /** @param list<string> $ids
     * @return array<string, MessageSendData> */
    public function many(string $accountId, array $ids): array;

    /** @param array{state: string, message_id?: ?string, error_code?: ?string, retry_after?: ?int, retryable?: bool, reply_dropped?: bool} $outcome */
    public function record(string $id, array $outcome): MessageSendData;
}
