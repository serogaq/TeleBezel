<?php

declare(strict_types=1);

namespace App\Repositories;

use App\Contracts\Repositories\MessageSendRepository as MessageSendRepositoryContract;
use App\Data\MessageSendData;
use App\Exceptions\ApiException;
use App\Models\MessageSend;
use App\Support\Values;
use Carbon\CarbonImmutable;
use Illuminate\Database\QueryException;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Str;

final class MessageSendRepository implements MessageSendRepositoryContract
{
    private const array SETTLED = ['sent', 'failed'];

    private function data(MessageSend $row): MessageSendData
    {
        return new MessageSendData(
            Values::string($row->getAttribute('id')),
            Values::string($row->getAttribute('token_id')),
            Values::string($row->getAttribute('account_id')),
            Values::string($row->getAttribute('storage_generation')),
            Values::integer($row->getAttribute('authorization_generation')),
            (string) Values::integer($row->getAttribute('chat_id')),
            $row->getAttribute('reply_to_message_id') === null ? null : (string) Values::integer($row->getAttribute('reply_to_message_id')),
            Values::string($row->getAttribute('request_hmac')),
            Values::string($row->getAttribute('state')),
            $row->getAttribute('message_id') === null ? null : (string) Values::integer($row->getAttribute('message_id')),
            $row->getAttribute('error_code') === null ? null : Values::string($row->getAttribute('error_code')),
            $row->getAttribute('retry_after') === null ? null : Values::integer($row->getAttribute('retry_after')),
            (bool) $row->getAttribute('retryable'),
            (bool) $row->getAttribute('reply_dropped'),
            CarbonImmutable::instance($row->updated_at ?? now()),
        );
    }

    /** @param array{instance_id: string, token_id: string, account_id: string, storage_generation: string, authorization_generation: int, chat_id: string, reply_to_message_id: ?string, key_hash: string, request_hmac: string} $attributes
     * @return array{MessageSendData, bool} */
    public function claim(array $attributes, int $attempt = 0): array
    {
        try {
            return DB::transaction(function () use ($attributes): array {
                $existing = MessageSend::query()->where('token_id', $attributes['token_id'])->where('account_id', $attributes['account_id'])->where('key_hash', $attributes['key_hash'])->lockForUpdate()->first();
                if ($existing !== null) {
                    if (! hash_equals(Values::string($existing->getAttribute('request_hmac')), $attributes['request_hmac'])) {
                        throw new ApiException('operation.conflict', 409);
                    }

                    return [$this->data($existing), false];
                }
                $row = MessageSend::query()->create([
                    ...$attributes,
                    'id' => (string) Str::uuid(),
                    'state' => 'dispatching',
                ]);

                return [$this->data($row->refresh()), true];
            }, 3);
        } catch (QueryException $exception) {
            if ($exception->getCode() !== '23505' || $attempt >= 2) {
                throw $exception;
            }

            return $this->claim($attributes, $attempt + 1);
        }
    }

    public function find(string $id): ?MessageSendData
    {
        $row = Str::isUuid($id) ? MessageSend::query()->find($id) : null;

        return $row === null ? null : $this->data($row);
    }

    public function owned(string $tokenId, string $accountId, string $id): ?MessageSendData
    {
        if (! Str::isUuid($id)) {
            return null;
        }
        $row = MessageSend::query()->whereKey($id)->where('token_id', $tokenId)->where('account_id', $accountId)->first();

        return $row === null ? null : $this->data($row);
    }

    /** @param list<string> $ids
     * @return array<string, MessageSendData> */
    public function many(string $accountId, array $ids): array
    {
        $ids = array_values(array_filter($ids, Str::isUuid(...)));
        if ($ids === []) {
            return [];
        }
        $result = [];
        foreach (MessageSend::query()->where('account_id', $accountId)->whereKey($ids)->get() as $row) {
            $data = $this->data($row);
            $result[$data->id] = $data;
        }

        return $result;
    }

    /** @param array{state: string, message_id?: ?string, error_code?: ?string, retry_after?: ?int, retryable?: bool, reply_dropped?: bool} $outcome */
    public function record(string $id, array $outcome): MessageSendData
    {
        return DB::transaction(function () use ($id, $outcome): MessageSendData {
            $row = MessageSend::query()->whereKey($id)->lockForUpdate()->firstOrFail();
            $current = Values::string($row->getAttribute('state'));
            if (in_array($current, self::SETTLED, true) || ($current === 'unknown' && ! in_array($outcome['state'], self::SETTLED, true))) {
                return $this->data($row);
            }
            if ($current === 'pending' && $outcome['state'] === 'dispatching') {
                return $this->data($row);
            }
            $row->forceFill([
                'state' => $outcome['state'],
                'message_id' => $outcome['message_id'] ?? $row->getAttribute('message_id'),
                'error_code' => $outcome['error_code'] ?? null,
                'retry_after' => $outcome['retry_after'] ?? null,
                'retryable' => $outcome['retryable'] ?? false,
                'reply_dropped' => $outcome['reply_dropped'] ?? false,
            ])->save();

            return $this->data($row);
        }, 3);
    }
}
