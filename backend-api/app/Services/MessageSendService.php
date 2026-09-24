<?php

declare(strict_types=1);

namespace App\Services;

use App\Contracts\Repositories\MessageSendRepository;
use App\Contracts\TdlibGateway;
use App\Data\AccountData;
use App\Data\MessageSendData;
use App\Data\PrincipalContext;
use App\Data\TelegramId;
use App\Enums\AccountLifecycle;
use App\Exceptions\ApiException;
use App\Support\MessageText;
use App\Support\Values;

final readonly class MessageSendService
{
    private const int NOT_FOUND_GRACE_SECONDS = 15;

    private const array STATES = ['pending', 'sent', 'failed', 'unknown'];

    public function __construct(private TelegramAccountService $accounts, private MessageSendRepository $sends, private TdlibGateway $tdlib, private string $appKey) {}

    public function send(PrincipalContext $principal, string $uuid, string $chatId, string $idempotencyKey, string $text, ?string $replyTo, string $requestId): MessageSendData
    {
        if (strlen($idempotencyKey) < 8 || strlen($idempotencyKey) > 200) {
            throw new ApiException('request.invalid_idempotency_key', 422);
        }
        $chat = new TelegramId($chatId, 'chat.not_found');
        $reply = $replyTo === null ? null : new TelegramId($replyTo);
        $canonical = MessageText::canonical($text);
        if ($canonical === null || $canonical === '') {
            throw new ApiException('request.invalid', 422);
        }
        if (MessageText::utf16Length($canonical) > MessageText::MAXIMUM_UTF16_UNITS) {
            throw new ApiException('message.text_too_long', 422);
        }
        $account = $this->sendable($uuid, $requestId);
        [$send, $created] = $this->sends->claim([
            'instance_id' => $principal->instanceId ?? throw new ApiException('auth.insufficient_scope', 403),
            'token_id' => $principal->id,
            'account_id' => $account->id,
            'storage_generation' => $account->storage_generation,
            'authorization_generation' => $account->authorization_generation,
            'chat_id' => $chat->value,
            'reply_to_message_id' => $reply?->value,
            'key_hash' => hash('sha256', $idempotencyKey),
            'request_hmac' => $this->requestHmac($chat->value, $reply?->value, $canonical),
        ]);
        if (! $created) {
            return $this->refresh($send, $requestId);
        }
        try {
            $result = $this->tdlib->sendMessage($account->id, $chat->value, [
                'operation_id' => $send->id,
                'storage_generation' => $send->storageGeneration,
                'authorization_generation' => $send->authorizationGeneration,
                'text' => $canonical,
                ...$reply === null ? [] : [
                    'reply_to_message_id' => $reply->value,
                ],
            ], $requestId);
        } catch (ApiException $exception) {
            return $this->sends->record($send->id, $this->failure($exception));
        }

        return $this->sends->record($send->id, $this->outcome($result));
    }

    public function status(PrincipalContext $principal, string $uuid, string $operationId, string $requestId): MessageSendData
    {
        $send = $this->sends->owned($principal->id, strtolower($uuid), strtolower($operationId)) ?? throw new ApiException('operation.not_found', 404);

        return $this->refresh($send, $requestId);
    }

    /** @param array<string, mixed> $event */
    public function observe(array $event): ?MessageSendData
    {
        $id = $event['operation_id'] ?? null;
        $send = is_string($id) ? $this->sends->find($id) : null;
        if ($send === null) {
            return null;
        }

        return $this->sends->record($send->id, $this->outcome($event));
    }

    /** @param list<string> $operationIds
     * @return array<string, MessageSendData> */
    public function known(string $accountId, array $operationIds): array
    {
        return $this->sends->many($accountId, $operationIds);
    }

    private function refresh(MessageSendData $send, string $requestId): MessageSendData
    {
        if ($send->settled()) {
            return $send;
        }
        try {
            $operations = Values::object($this->tdlib->sendStatus($send->accountId, [$send->id], $requestId)['operations'] ?? []);
        } catch (ApiException) {
            return $send;
        }
        $operation = is_array($operations[$send->id] ?? null) ? Values::object($operations[$send->id]) : [];
        if (($operation['state'] ?? null) === 'not_found') {
            if ($send->state === 'unknown' || $send->updatedAt->addSeconds(self::NOT_FOUND_GRACE_SECONDS)->isFuture()) {
                return $send;
            }

            return $this->sends->record($send->id, [
                'state' => 'unknown',
                'error_code' => 'operation.outcome_unknown',
            ]);
        }

        return $operation === [] ? $send : $this->sends->record($send->id, $this->outcome($operation));
    }

    private function sendable(string $uuid, string $requestId): AccountData
    {
        $account = $this->accounts->find($uuid, false, $requestId);
        if ($account->lifecycle !== AccountLifecycle::Active) {
            throw new ApiException('authorization.invalid_state', 409);
        }

        return $account;
    }

    /** @return array{state: string, error_code: string, retryable: bool, retry_after?: ?int} */
    private function failure(ApiException $exception): array
    {
        if ($exception->errorCode === 'operation.outcome_unknown') {
            return [
                'state' => 'unknown',
                'error_code' => 'operation.outcome_unknown',
                'retryable' => false,
            ];
        }

        return [
            'state' => 'failed',
            'error_code' => $exception->errorCode,
            'retryable' => in_array($exception->errorCode, ['service.busy', 'service.stopping', 'service.tdlib_unavailable'], true),
            'retry_after' => $exception->retryAfter,
        ];
    }

    /** @param array<string, mixed> $operation
     * @return array{state: string, message_id: ?string, error_code: ?string, retry_after: ?int, retryable: bool, reply_dropped: bool} */
    private function outcome(array $operation): array
    {
        $state = $operation['state'] ?? null;
        if (! is_string($state) || ! in_array($state, self::STATES, true)) {
            $state = 'unknown';
        }
        $error = is_array($operation['error'] ?? null) ? $operation['error'] : [];
        $code = $error['code'] ?? null;
        $retryAfter = $error['retry_after'] ?? null;
        $messageId = $operation['message_id'] ?? null;

        return [
            'state' => $state,
            'message_id' => is_string($messageId) && preg_match('/^-?[1-9][0-9]{0,18}$/D', $messageId) === 1 ? $messageId : null,
            'error_code' => is_string($code) && preg_match('/^[a-z_]+\.[a-z_]+$/D', $code) === 1 ? $code : ($state === 'failed' ? 'message.send_failed' : null),
            'retry_after' => is_int($retryAfter) && $retryAfter > 0 ? min($retryAfter, 86400) : null,
            'retryable' => ($operation['retryable'] ?? false) === true,
            'reply_dropped' => ($operation['reply_dropped'] ?? false) === true,
        ];
    }

    private function requestHmac(string $chat, ?string $reply, string $text): string
    {
        $key = hash_hmac('sha256', 'telebezel/message-sends/v1', $this->appKey, true);

        return hash_hmac('sha256', json_encode([
            'chat' => $chat,
            'reply_to' => $reply,
            'text' => $text,
        ], JSON_THROW_ON_ERROR | JSON_UNESCAPED_UNICODE), $key);
    }
}
