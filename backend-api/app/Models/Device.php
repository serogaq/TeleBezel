<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Concerns\HasUuids;
use Illuminate\Database\Eloquent\Model;

final class Device extends Model
{
    use HasUuids;

    protected $fillable = ['id', 'instance_id', 'name', 'token_prefix', 'token_hash', 'locale', 'default_account_id', 'chat_list', 'last_seen_at', 'revoked_at'];

    protected $hidden = ['token_hash'];

    protected function casts(): array
    {
        return [
            'last_seen_at' => 'immutable_datetime',
            'revoked_at' => 'immutable_datetime',
        ];
    }
}
