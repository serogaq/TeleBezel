<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

final class MessageSend extends Model
{
    public $incrementing = false;

    protected $keyType = 'string';

    protected $fillable = ['id', 'instance_id', 'token_id', 'account_id', 'storage_generation', 'authorization_generation', 'chat_id', 'reply_to_message_id', 'key_hash', 'request_hmac', 'state', 'message_id', 'error_code', 'retry_after', 'retryable', 'reply_dropped'];

    protected $hidden = ['key_hash', 'request_hmac'];

    protected function casts(): array
    {
        return [
            'authorization_generation' => 'integer',
            'retry_after' => 'integer',
            'retryable' => 'boolean',
            'reply_dropped' => 'boolean',
        ];
    }
}
