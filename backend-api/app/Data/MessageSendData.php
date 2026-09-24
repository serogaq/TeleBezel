<?php

declare(strict_types=1);

namespace App\Data;

use Carbon\CarbonImmutable;

final readonly class MessageSendData
{
    public function __construct(
        public string $id,
        public string $tokenId,
        public string $accountId,
        public string $storageGeneration,
        public int $authorizationGeneration,
        public string $chatId,
        public ?string $replyTo,
        public string $requestHmac,
        public string $state,
        public ?string $messageId,
        public ?string $errorCode,
        public ?int $retryAfter,
        public bool $retryable,
        public bool $replyDropped,
        public CarbonImmutable $updatedAt,
    ) {}

    public function settled(): bool
    {
        return $this->state === 'sent' || $this->state === 'failed';
    }

    /** @return array<string, mixed> */
    public function toArray(): array
    {
        return [
            'id' => $this->id,
            'state' => $this->state,
            'chat_id' => $this->chatId,
            'reply_to_message_id' => $this->replyTo,
            'message_id' => $this->messageId,
            'error' => $this->errorCode === null ? null : [
                'code' => $this->errorCode,
                'retry_after' => $this->retryAfter,
            ],
            'retryable' => $this->retryable,
            'reply_dropped' => $this->replyDropped,
        ];
    }
}
