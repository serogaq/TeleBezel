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
            $data = self::pick($data, ['items', 'events', 'stale', 'partial', 'has_more', 'next_cursor', 'retry_cursor', 'updates_cursor', 'observed_at', 'source', 'cursor', 'refresh', 'connection', 'local_exhausted', 'fallback_reason', 'unread', 'status']);
            if (is_array($data['unread'] ?? null)) {
                $data['unread'] = self::pick(Values::object($data['unread']), ['chats', 'messages']);
            }
            if (is_array($data['status'] ?? null)) {
                $data['status'] = self::pick(Values::object($data['status']), ['connection', 'proxy']);
            }
            foreach (['items', 'events'] as $key) {
                if (isset($data[$key]) && is_array($data[$key])) {
                    $data[$key] = array_values(array_map(function (mixed $item) use ($kind): array {
                        $item = Values::object($item);

                        return match ($kind) {
                            'chats' => self::chat($item),
                            'messages' => self::message($item),
                            default => self::event($item),
                        };
                    }, $data[$key]));
                }
            }
        }
        parent::__construct($data);
    }

    /** @param array<string, mixed> $value
     * @return array<string, mixed> */
    private static function event(array $value): array
    {
        $value = self::pick($value, ['type', 'chat_id', 'message_id', 'sequence', 'field', 'operation_id', 'state', 'error', 'retryable', 'reply_dropped', 'connection']);
        if (is_array($value['error'] ?? null)) {
            $value['error'] = self::pick(Values::object($value['error']), ['code', 'retry_after']);
        }

        return $value;
    }

    /** @param array<string, mixed> $value
     * @return array<string, mixed> */
    private static function chat(array $value): array
    {
        $value = self::pick($value, ['id', 'type', 'title', 'is_saved_messages', 'is_forum', 'is_marked_unread', 'unread_count', 'unread_mention_count', 'unread_reaction_count', 'notifications', 'last_read_inbox_message_id', 'last_read_outbox_message_id', 'positions', 'last_message', 'can_send', 'stale', 'observed_at', 'source']);
        if (is_array($value['can_send'] ?? null)) {
            $value['can_send'] = self::pick(Values::object($value['can_send']), ['text', 'reason']);
        }
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
        $value = self::pick($value, ['id', 'chat_id', 'sender', 'date', 'edit_date', 'is_outgoing', 'author_signature', 'forward_from', 'reply_to', 'sending_state', 'content', 'stale', 'observed_at', 'source']);
        if (is_array($value['reply_to'] ?? null)) {
            $value['reply_to'] = self::pick(Values::object($value['reply_to']), ['message_id', 'sender_name', 'text']);
        }
        if (is_array($value['sender'] ?? null)) {
            $value['sender'] = self::pick(Values::object($value['sender']), ['type', 'id', 'name', 'fallback']);
        }
        if (is_array($value['forward_from'] ?? null)) {
            $value['forward_from'] = self::pick(Values::object($value['forward_from']), ['type', 'id', 'name', 'fallback', 'signature']);
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
