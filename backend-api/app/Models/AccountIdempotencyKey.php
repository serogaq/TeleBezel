<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

final class AccountIdempotencyKey extends Model
{
    public $incrementing = false;

    protected $primaryKey = null;

    protected $fillable = ['api_client_id', 'key_hash', 'request_hash', 'telegram_account_id'];
}
