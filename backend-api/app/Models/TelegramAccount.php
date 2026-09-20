<?php

namespace App\Models;

use App\Enums\AccountLifecycle;
use Illuminate\Database\Eloquent\Concerns\HasUuids;
use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\SoftDeletes;

/**
 * @property AccountLifecycle $lifecycle
 * @property string|null $proxy_id
 * @property string|null $proxy_server
 * @property int|null $proxy_port
 * @property string|null $proxy_type
 * @property int $desired_revision
 * @property int|null $applied_revision
 * @property bool $runtime_available
 * @property array<string, mixed>|null $telegram_identity
 */
final class TelegramAccount extends Model
{
    use HasUuids;
    use SoftDeletes;

    protected $fillable = [
        'id', 'label', 'storage_generation', 'lifecycle', 'desired_revision',
        'applied_revision', 'proxy_id', 'proxy_server', 'proxy_port', 'proxy_type', 'runtime_available',
        'authorization_state', 'connection_state',
        'last_error_code', 'operation_id', 'logout_operation_id',
        'logout_completed_at',
    ];

    protected function casts(): array
    {
        return [
            'lifecycle' => AccountLifecycle::class,
            'desired_revision' => 'integer',
            'applied_revision' => 'integer',
            'proxy_port' => 'integer',
            'runtime_available' => 'boolean',
            'logout_completed_at' => 'immutable_datetime',
            'deleted_at' => 'immutable_datetime',
        ];
    }
}
