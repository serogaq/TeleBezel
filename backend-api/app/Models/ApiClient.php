<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Concerns\HasUuids;
use Illuminate\Database\Eloquent\Model;

final class ApiClient extends Model
{
    use HasUuids;

    protected $fillable = ['name', 'token_prefix', 'token_hash', 'revoked_at'];

    protected function casts(): array
    {
        return [
            'revoked_at' => 'immutable_datetime',
        ];
    }
}
