<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Concerns\HasUuids;
use Illuminate\Database\Eloquent\Model;
use Illuminate\Support\Carbon;

/** @property Carbon|null $last_ping_at
 * @property array<string, mixed>|null $credentials
 */
final class ProxyProfile extends Model
{
    use HasUuids;

    protected $fillable = ['id', 'instance_id', 'label', 'mode', 'host', 'port', 'http_only', 'username', 'credentials', 'position', 'last_ping_ok', 'last_ping_ms', 'last_ping_error', 'last_ping_at'];

    protected $hidden = ['credentials'];

    protected function casts(): array
    {
        return [
            'credentials' => 'encrypted:array',
            'http_only' => 'boolean',
            'last_ping_ok' => 'boolean',
            'port' => 'integer',
            'position' => 'integer',
            'last_ping_ms' => 'integer',
            'last_ping_at' => 'datetime',
        ];
    }
}
