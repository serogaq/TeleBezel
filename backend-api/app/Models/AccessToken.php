<?php

namespace App\Models;

use App\Enums\TokenType;
use Carbon\CarbonImmutable;
use Illuminate\Database\Eloquent\Concerns\HasUuids;
use Illuminate\Database\Eloquent\Model;

/**
 * @property TokenType $type
 * @property array<string, mixed> $claims
 * @property CarbonImmutable|null $expires_at
 * @property int|null $idle_timeout_seconds
 * @property CarbonImmutable|null $last_active_at
 * @property CarbonImmutable|null $last_used_at
 * @property CarbonImmutable|null $revoked_at
 */
final class AccessToken extends Model
{
    use HasUuids;

    protected $fillable = ['id', 'instance_id', 'name', 'type', 'token_prefix', 'token_hash', 'claims', 'expires_at', 'idle_timeout_seconds', 'last_active_at', 'last_used_at', 'revoked_at'];

    protected $hidden = ['token_hash'];

    protected function casts(): array
    {
        return [
            'type' => TokenType::class,
            'claims' => 'array',
            'expires_at' => 'immutable_datetime',
            'idle_timeout_seconds' => 'integer',
            'last_active_at' => 'immutable_datetime',
            'last_used_at' => 'immutable_datetime',
            'revoked_at' => 'immutable_datetime',
        ];
    }
}
