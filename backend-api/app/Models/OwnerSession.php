<?php

namespace App\Models;

use Carbon\CarbonImmutable;
use Illuminate\Database\Eloquent\Concerns\HasUuids;
use Illuminate\Database\Eloquent\Model;

/** @property CarbonImmutable $authenticated_at
 * @property CarbonImmutable $last_interactive_at
 * @property CarbonImmutable $expires_at
 */
final class OwnerSession extends Model
{
    use HasUuids;

    protected $fillable = ['id', 'instance_id', 'token_hash', 'authenticated_at', 'last_interactive_at', 'expires_at', 'revoked_at'];

    protected function casts(): array
    {
        return ['authenticated_at' => 'immutable_datetime', 'last_interactive_at' => 'immutable_datetime', 'expires_at' => 'immutable_datetime', 'revoked_at' => 'immutable_datetime'];
    }
}
