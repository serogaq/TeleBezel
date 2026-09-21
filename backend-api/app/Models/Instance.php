<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Concerns\HasUuids;
use Illuminate\Database\Eloquent\Model;
use Illuminate\Support\Carbon;

/**
 * @property int $proxy_connect_timeout_seconds
 * @property Carbon|null $proxy_failure_started_at
 */
final class Instance extends Model
{
    use HasUuids;

    protected $fillable = ['id', 'owner_password_hash', 'recovery_code_hash', 'app_key_check', 'telegram_api_id', 'telegram_api_hash', 'configuration_revision',
        'active_proxy_profile_id', 'proxy_failure_action', 'proxy_connect_timeout_seconds', 'proxy_activated_at', 'proxy_failure_started_at'];

    protected $hidden = ['owner_password_hash', 'recovery_code_hash', 'app_key_check', 'telegram_api_hash'];

    protected function casts(): array
    {
        return ['app_key_check' => 'encrypted', 'telegram_api_hash' => 'encrypted',
            'configuration_revision' => 'integer', 'proxy_connect_timeout_seconds' => 'integer',
            'proxy_activated_at' => 'datetime', 'proxy_failure_started_at' => 'datetime'];
    }
}
