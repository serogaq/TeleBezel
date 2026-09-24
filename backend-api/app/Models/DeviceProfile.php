<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

final class DeviceProfile extends Model
{
    public $incrementing = false;

    protected $primaryKey = 'token_id';

    protected $keyType = 'string';

    protected $fillable = ['token_id', 'locale', 'default_account_id', 'chat_list'];
}
