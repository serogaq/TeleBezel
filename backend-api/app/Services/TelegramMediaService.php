<?php

declare(strict_types=1);

namespace App\Services;

use App\Cache\MediaCacheKeys;
use App\Contracts\MediaRenderer;
use App\Contracts\RenditionStore;
use App\Contracts\TdlibGateway;
use App\Data\Input;
use App\Data\MediaSpec;
use App\Data\TelegramId;
use App\Enums\AccountLifecycle;
use App\Exceptions\ApiException;
use App\Support\Tbi;

// Prepares a message image for a watch. Every request is authorised and
// checked against Telegram first; the rendition cache only saves the work.
final readonly class TelegramMediaService
{
    public const string PIPELINE = '1';

    private const int SOURCE_LIMIT = 1048576;

    private const array STATES = ['ready', 'downloading', 'preparing', 'spoiler', 'restricted', 'unsupported', 'unavailable', 'none'];

    public function __construct(private TelegramAccountService $accounts, private TdlibGateway $tdlib, private MediaRenderer $renderer, private RenditionStore $renditions) {}

    /** @return array<string, mixed> */
    public function media(string $uuid, string $chatId, string $messageId, Input $input, string $requestId): array
    {
        $chat = new TelegramId($chatId, 'chat.not_found');
        $message = new TelegramId($messageId, 'message.not_found');
        $account = $this->accounts->find($uuid, false, $requestId);
        if ($account->lifecycle !== AccountLifecycle::Active) {
            throw new ApiException('authorization.invalid_state', 409);
        }
        $spec = MediaSpec::fromInput($input);
        $query = [
            'index' => $input->has('index') ? $input->integer('index') : 0,
            'min_side' => max($spec->width, $spec->height),
            'reveal' => $input->has('reveal') && $input->string('reveal') === '1' ? 1 : 0,
        ];
        $descriptor = $this->tdlib->media($uuid, $chat->value, $message->value, $query + [
            'bytes' => 0,
        ], $requestId);
        $result = $this->describe($descriptor);
        if ($result['state'] !== 'ready') {
            return $result;
        }
        $key = $this->key($uuid, $descriptor, $spec);
        $rendition = $this->renditions->get($uuid, $key);
        $header = $rendition === null ? null : Tbi::inspect($rendition, $spec);
        if ($rendition === null || $header === null) {
            $source = $this->tdlib->media($uuid, $chat->value, $message->value, $query + [
                'bytes' => 1,
            ], $requestId);
            $fresh = $this->describe($source);
            if ($fresh['state'] !== 'ready' || $fresh['item_message_id'] !== $result['item_message_id']) {
                return $fresh;
            }
            $encoded = $source['bytes_base64'] ?? null;
            $bytes = is_string($encoded) && strlen($encoded) <= intdiv(self::SOURCE_LIMIT * 4, 3) + 4 ? base64_decode($encoded, true) : false;
            if ($bytes === false || $bytes === '' || strlen($bytes) > self::SOURCE_LIMIT) {
                throw new ApiException('service.tdlib_unavailable', 503);
            }
            try {
                $rendition = $this->renderer->render($bytes, $spec, $requestId);
            } catch (ApiException $exception) {
                if ($exception->errorCode === 'media.unsupported') {
                    return [
                        'state' => 'unsupported',
                        'retry_after' => null,
                    ] + $result;
                }
                if ($exception->errorCode === 'media.busy') {
                    return [
                        'state' => 'preparing',
                        'retry_after' => 1,
                    ] + $result;
                }
                throw $exception;
            }
            $header = Tbi::inspect($rendition, $spec);
            if ($header === null) {
                throw new ApiException('service.media_unavailable', 503);
            }
            $this->renditions->put($uuid, $this->key($uuid, $source, $spec), $rendition);
        }

        return $result + [
            'rendition' => [
                'tag' => $header['tag'],
                'width' => $header['width'],
                'height' => $header['height'],
                'shape' => $header['shape'],
                'format' => 'p'.$header['bits'],
                'crc32' => $header['crc'],
                'bytes_base64' => base64_encode($rendition),
            ],
        ];
    }

    /** @param array<string, mixed> $descriptor
     * @return array{state: string, retry_after: int|null, index: int, count: int, item_message_id: string, has_spoiler: bool}
     */
    private function describe(array $descriptor): array
    {
        $state = $descriptor['state'] ?? null;
        $index = $descriptor['index'] ?? null;
        $count = $descriptor['count'] ?? null;
        $item = $descriptor['item_message_id'] ?? null;
        $retry = $descriptor['retry_after'] ?? null;
        if (! is_string($state) || ! in_array($state, self::STATES, true) || ! is_int($index) || ! is_int($count) || $count < 1 || $count > 10
            || $index < 0 || $index >= $count || ! is_string($item) || preg_match('/\A[1-9][0-9]{0,18}\z/', $item) !== 1) {
            throw new ApiException('service.tdlib_unavailable', 503);
        }

        return [
            'state' => $state,
            'retry_after' => is_int($retry) && $retry > 0 ? min($retry, 30) : null,
            'index' => $index,
            'count' => $count,
            'item_message_id' => $item,
            'has_spoiler' => ($descriptor['has_spoiler'] ?? false) === true,
        ];
    }

    /** @param array<string, mixed> $descriptor */
    private function key(string $uuid, array $descriptor, MediaSpec $spec): string
    {
        $fence = is_array($descriptor['fence'] ?? null) ? $descriptor['fence'] : [];
        $source = is_array($descriptor['source'] ?? null) ? $descriptor['source'] : [];
        $text = static fn (mixed $value): string => is_scalar($value) ? (string) $value : '';
        $unique = $text($source['unique_id'] ?? null);
        if ($unique === '' || $text($fence['runtime_epoch'] ?? null) === '') {
            throw new ApiException('service.tdlib_unavailable', 503);
        }

        return MediaCacheKeys::rendition($uuid, $text($fence['storage_generation'] ?? null), $text($fence['authorization_generation'] ?? null),
            $text($fence['runtime_epoch']), $text($descriptor['item_message_id'] ?? null), $unique, $spec->canonical(), self::PIPELINE);
    }
}
