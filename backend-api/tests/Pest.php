<?php

use App\Enums\TokenType;
use App\Models\AccessToken;
use App\Models\DeviceProfile;
use App\Models\Instance;
use App\Services\AuthenticationService;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Http;
use Illuminate\Support\Str;
use Tests\TestCase;

pest()->extend(TestCase::class)->beforeEach(fn () => Http::preventStrayRequests())->in('Feature');
pest()->use(RefreshDatabase::class)->in(
    'Feature/HealthTest.php', 'Feature/ApiClientCommandTest.php', 'Feature/StatusTest.php',
    'Feature/TelegramAccountsTest.php', 'Feature/TelegramReadContractTest.php', 'Feature/OwnerAccessTest.php',
    'Feature/AuditHardeningTest.php', 'Feature/AccessTokenPermissionsTest.php', 'Feature/MessageSendTest.php',
);

pest()->extend(TestCase::class)->use(RefreshDatabase::class)->in('Integration');

function tdlibReadyStatusFixture(): array
{
    return json_decode((string) file_get_contents(base_path('../backend-tdlib/tests/contracts/tdlib_status_ready.json')), true, flags: JSON_THROW_ON_ERROR);
}

function testInstance(): Instance
{
    return Instance::query()->first() ?? Instance::query()->create([]);
}

/** @param list<string>|null $permissions
 * @param '*'|list<string> $accounts
 * @return array{token: string, id: string, instance_id: string} */
function issueTestToken(TokenType $type = TokenType::Maintenance, ?array $permissions = null, string|array $accounts = '*', array $attributes = []): array
{
    $instance = testInstance();
    $token = 'tb_'.Str::random(43);
    $model = AccessToken::query()->create([
        'instance_id' => $instance->id,
        'name' => $type->value,
        'type' => $type,
        'token_prefix' => substr($token, 0, 12),
        'token_hash' => hash('sha256', $token),
        'claims' => [
            'permissions' => $permissions ?? $type->permissionNames(),
            'accounts' => $accounts,
        ],
        ...$attributes,
    ]);
    if ($type === TokenType::Device) {
        DeviceProfile::query()->create([
            'token_id' => $model->id,
        ]);
    }

    return [
        'token' => $token,
        'id' => $model->id,
        'instance_id' => $model->instance_id,
    ];
}

/** @return array{token: string, id: string, instance_id: string} */
function issueWebSession(?Instance $instance = null): array
{
    return issueTestToken(TokenType::Maintenance, attributes: [
        'instance_id' => ($instance ?? testInstance())->id,
        'name' => 'Web settings',
        'expires_at' => now()->addHours(12),
        'idle_timeout_seconds' => 1800,
        'last_active_at' => now(),
    ]);
}

function asWebSession(Illuminate\Foundation\Testing\TestCase $test, string $token): Illuminate\Foundation\Testing\TestCase
{
    return $test->withoutToken()->withCredentials()->withUnencryptedCookie(AuthenticationService::COOKIE, $token)->withHeader(AuthenticationService::CSRF_HEADER, app(AuthenticationService::class)->csrf($token));
}
