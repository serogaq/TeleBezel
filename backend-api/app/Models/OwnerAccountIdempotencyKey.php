<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

final class OwnerAccountIdempotencyKey extends Model
{
    public $incrementing = false;

    protected $primaryKey = null;

    protected $fillable = ['owner_session_id', 'key_hash', 'request_hash', 'telegram_account_id'];
}
