<?php

declare(strict_types=1);

namespace App\Data;

use App\Enums\AccountLifecycle;
use Carbon\CarbonImmutable;

final class AccountData
{
    /** @var array<string, mixed>|null */
    public ?array $telegram_identity = null;

    /** @param array<string, mixed>|null $proxy_config */
    public function __construct(public string $id, public ?string $label, public string $storage_generation, public AccountLifecycle $lifecycle, public int $desired_revision, public ?int $applied_revision, public ?string $proxy_id, public ?string $proxy_server, public ?int $proxy_port, public ?string $proxy_type, public ?array $proxy_config, public int $proxy_config_version, public bool $runtime_available, public ?string $authorization_state, public ?string $connection_state, public ?string $last_error_code, public ?string $operation_id, public ?string $logout_operation_id, public int $authorization_generation, public ?string $effective_config_id, public ?CarbonImmutable $created_at, public ?CarbonImmutable $updated_at, public ?CarbonImmutable $next_reconcile_at, public int $reconcile_failures, public ?int $reconcile_blocked_revision) {}
}
