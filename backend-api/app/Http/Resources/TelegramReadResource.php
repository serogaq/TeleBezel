<?php

declare(strict_types=1);

namespace App\Http\Resources;

use App\Support\Values;
use stdClass;

final class TelegramReadResource extends ApiResource
{
    /** @param array<string, mixed> $data */
    public function __construct(array $data, string $kind)
    {
        if ($kind === 'chat' || $kind === 'message') {
            $data = self::pick($data, ['item', 'stale', 'partial', 'updates_cursor', 'source', 'observed_at', 'refresh', 'connection', 'fallback_reason']);
            if (is_array($data['item'] ?? null)) {
                $item = Values::object($data['item']);
                $data['item'] = $kind === 'chat' ? self::chat($item) : self::message($item);
            }
        } elseif ($kind === 'interest') {
            $data = self::pick($data, ['active', 'expires_in', 'expires_in_seconds', 'lease_seconds', 'released', 'chat_id']);
        } else {
            $data = self::pick($data, ['items', 'events', 'stale', 'partial', 'has_more', 'next_cursor', 'retry_cursor', 'updates_cursor', 'observed_at', 'source', 'cursor', 'refresh', 'connection', 'local_exhausted', 'fallback_reason']);
            foreach (['items', 'events'] as $key) {
                if (isset($data[$key]) && is_array($data[$key])) {
                    $data[$key] = array_values(array_map(function (mixed $item) use ($kind): array {
                        $item = Values::object($item);

                        return match ($kind) {
                            'chats' => self::chat($item),
                            'messages' => self::message($item),
                            default => self::pick($item, ['type', 'chat_id', 'message_id', 'sequence', 'field']),
                        };
                    }, $data[$key]));
                }
            }
        }
        parent::__construct($data);
    }

    /** @param array<string, mixed> $value
     * @return array<string, mixed> */
    private static function chat(array $value): array
    {
        $value = self::pick($value, ['id', 'type', 'title', 'is_forum', 'is_marked_unread', 'unread_count', 'unread_mention_count', 'unread_reaction_count', 'notifications', 'last_read_inbox_message_id', 'last_read_outbox_message_id', 'positions', 'last_message', 'stale', 'observed_at', 'source']);
        if (is_array($value['notifications'] ?? null)) {
            $value['notifications'] = self::pick(Values::object($value['notifications']), ['use_default_mute_for', 'mute_for']);
        }
        if (is_array($value['last_message'] ?? null)) {
            $value['last_message'] = self::message(Values::object($value['last_message']));
        }
        if (is_array($value['positions'] ?? null)) {
            $positions = [];
            foreach (['main', 'archive'] as $list) {
                $position = $value['positions'][$list] ?? null;
                if (is_array($position)) {
                    $positions[$list] = self::pick(Values::object($position), ['order', 'is_pinned']);
                }
            }
            $value['positions'] = $positions === [] ? new stdClass : $positions;
        }

        return $value;
    }

    /** @param array<string, mixed> $value
     * @return array<string, mixed> */
    private static function message(array $value): array
    {
        $value = self::pick($value, ['id', 'chat_id', 'sender', 'date', 'edit_date', 'is_outgoing', 'author_signature', 'content', 'stale', 'observed_at', 'source']);
        if (is_array($value['sender'] ?? null)) {
            $value['sender'] = self::pick(Values::object($value['sender']), ['type', 'id', 'name', 'fallback']);
        }
        if (is_array($value['content'] ?? null)) {
            $value['content'] = self::pick(Values::object($value['content']), ['kind', 'text', 'fallback_key', 'duration', 'emoji', 'title', 'action', 'preview_id', 'preview_state']);
        }

        return $value;
    }

    /** @param array<string, mixed> $value
     * @param list<string> $keys
     * @return array<string, mixed> */
    private static function pick(array $value, array $keys): array
    {
        return array_intersect_key($value, array_flip($keys));
    }
}
