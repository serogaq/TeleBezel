<?php

use App\Contracts\TdlibGateway;
use App\Enums\AccountLifecycle;
use App\Exceptions\ApiException;
use App\Exceptions\ExceptionEnvelope;
use App\Infrastructure\Tdlib\TdlibGateway as HttpTdlibGateway;
use App\Models\ApiClient;
use App\Models\Device;
use App\Models\Instance;
use App\Models\TelegramAccount;
use App\Services\ReconciliationService;
use Illuminate\Console\Scheduling\Event;
use Illuminate\Console\Scheduling\Schedule;
use Illuminate\Database\QueryException;
use Illuminate\Http\Client\Request as ClientRequest;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Http;
use Illuminate\Support\Str;
use Tests\Fakes\FakeTdlibGateway;

beforeEach(function (): void {
    config([
        'app.url' => 'https://telebezel.test',
        'cache.default' => 'array',
    ]);
    $this->token = 'tb_'.str_repeat('h', 43);
    ApiClient::query()->create([
        'name' => 'audit-test',
        'token_prefix' => substr($this->token, 0, 12),
        'token_hash' => hash('sha256', $this->token),
    ]);
});

function auditAccount(AccountLifecycle $lifecycle = AccountLifecycle::Active, array $attributes = []): TelegramAccount
{
    return TelegramAccount::query()->create([
        'label' => 'Audit',
        'storage_generation' => (string) Str::uuid(),
        'lifecycle' => $lifecycle,
        'desired_revision' => 1,
        'effective_config_id' => (string) Str::uuid(),
        ...$attributes,
    ]);
}

function auditOwner(Instance $instance): string
{
    $token = 'tbo_'.Str::random(48);
    DB::table('owner_sessions')->insert([
        'id' => (string) Str::uuid(),
        'instance_id' => $instance->id,
        'token_hash' => hash('sha256', $token),
        'authenticated_at' => now(),
        'last_interactive_at' => now(),
        'expires_at' => now()->addHour(),
        'created_at' => now(),
        'updated_at' => now(),
    ]);

    return $token;
}

test('account rate limit cannot be bypassed by changing uuid case', function (): void {
    $account = auditAccount();
    Http::fake([
        '*' => Http::response([
            'data' => [
                'state' => 'awaiting_code',
            ],
        ], 202),
    ]);
    $variants = [$account->id, strtoupper($account->id), ucfirst($account->id), strtoupper($account->id)];
    $statuses = array_map(fn (string $id): int => $this->withToken($this->token)->postJson("/v1/telegram/accounts/{$id}/authorization/actions", [
        'action' => 'submit_phone_number',
        'authorization_version' => '1',
        'value' => '+15550000000',
    ])->status(), $variants);
    expect($statuses[3])->toBe(429)
        ->and(array_slice($statuses, 0, 3))->not->toContain(429);
});

test('malformed account identifiers are a not-found envelope, never a database error', function (): void {
    $this->withToken($this->token)->getJson('/v1/telegram/accounts/not-a-uuid')
        ->assertNotFound()->assertJsonPath('error.code', 'account.not_found')->assertJsonStructure(['request_id']);
    $this->withToken($this->token)->getJson('/v1/telegram/accounts/not-a-uuid/chats')
        ->assertNotFound()->assertJsonPath('error.code', 'account.not_found');
    $this->withToken($this->token)->getJson('/v1/unknown-route')
        ->assertNotFound()->assertJsonPath('error.code', 'http.not_found');
});

test('owner resources that do not exist are resource-specific not-found errors', function (): void {
    $owner = $this->withCredentials()->withCookie('telebezel_owner', auditOwner(Instance::query()->create([])));
    $owner->postJson('/v1/owner/proxies/'.Str::uuid().'/ping')
        ->assertNotFound()->assertJsonPath('error.code', 'proxy.not_found')->assertJsonStructure(['request_id']);
    $owner->postJson('/v1/owner/proxies/garbage/ping')
        ->assertNotFound()->assertJsonPath('error.code', 'proxy.not_found');
    $owner->deleteJson('/v1/owner/devices/garbage')
        ->assertNotFound()->assertJsonPath('error.code', 'device.not_found');
});

test('database errors are classified instead of reported as an outage', function (): void {
    $failure = function (string $state): QueryException {
        $cause = new PDOException('failure');
        (new ReflectionProperty(Exception::class, 'code'))->setValue($cause, $state);

        return new QueryException('pgsql', 'select 1', [], $cause);
    };
    expect(ExceptionEnvelope::query($failure('22P02')))->toMatchObject([
        'errorCode' => 'request.invalid',
        'status' => 422,
    ])
        ->and(ExceptionEnvelope::query($failure('23505')))->toMatchObject([
            'errorCode' => 'operation.conflict',
            'status' => 409,
        ])
        ->and(ExceptionEnvelope::query($failure('08006')))->toMatchObject([
            'errorCode' => 'service.database_unavailable',
            'status' => 503,
        ]);
});

test('an undecryptable stored secret is reported as a key problem', function (): void {
    $instance = Instance::query()->create([]);
    DB::table('instances')->where('id', $instance->id)->update([
        'telegram_api_hash' => 'not-encrypted-with-this-key',
    ]);
    $this->withCredentials()->withCookie('telebezel_owner', auditOwner($instance))->getJson('/v1/owner/settings')
        ->assertStatus(503)->assertJsonPath('error.code', 'storage.invalid_key');
});

test('device tokens cannot read authorization or proxy details', function (): void {
    $instance = Instance::query()->create([]);
    $token = 'tb_'.str_repeat('w', 43);
    Device::query()->create([
        'instance_id' => $instance->id,
        'name' => 'Watch',
        'token_prefix' => substr($token, 0, 12),
        'token_hash' => hash('sha256', $token),
    ]);
    $account = auditAccount();
    $this->withToken($token)->getJson("/v1/telegram/accounts/{$account->id}/authorization")->assertForbidden();
    $this->withToken($token)->getJson("/v1/telegram/accounts/{$account->id}/proxy")->assertForbidden();
});

test('the proxy monitor and retention purge are scheduled', function (): void {
    $commands = collect(app(Schedule::class)->events())->map(fn (Event $event): string => (string) $event->command)->implode("\n");
    expect($commands)->toContain('telebezel:proxy-monitor')->toContain('telebezel:accounts-reconcile')->toContain('telebezel:purge-expired');
});

test('removal finishes when the runtime no longer knows the account', function (): void {
    $gateway = new FakeTdlibGateway;
    $this->app->instance(TdlibGateway::class, $gateway);
    $account = auditAccount(AccountLifecycle::Removing, [
        'operation_id' => (string) Str::uuid(),
    ]);
    $gateway->queue('remove', new ApiException('account.gone', 410));
    app(ReconciliationService::class)->run(false);
    expect(TelegramAccount::withTrashed()->findOrFail($account->id)->lifecycle)->toBe(AccountLifecycle::Removed);
    $gateway->assertDrained();
});

test('a logout the runtime cannot find is abandoned and the account is provisioned again', function (): void {
    $gateway = new FakeTdlibGateway;
    $this->app->instance(TdlibGateway::class, $gateway);
    $operation = (string) Str::uuid();
    $account = auditAccount(AccountLifecycle::LogoutPending, [
        'desired_revision' => 2,
        'operation_id' => $operation,
        'logout_operation_id' => $operation,
    ]);
    $gateway->queue('logout', new ApiException('account.not_found', 404));
    $gateway->queue('provision', fn (array $arguments): array => [
        'applied_revision' => $arguments['command']['revision'],
        'runtime_available' => true,
    ]);
    $gateway->queue('listSnapshots', [
        'accounts' => [],
    ]);
    $gateway->queue('provision', fn (array $arguments): array => [
        'applied_revision' => $arguments['command']['revision'],
        'runtime_available' => true,
    ]);
    app(ReconciliationService::class)->run(false);
    $fresh = $account->fresh();
    expect($fresh->lifecycle)->toBe(AccountLifecycle::Active)
        ->and($fresh->desired_revision)->toBe(3)
        ->and($gateway->calls[1]['arguments']['command']['mode'])->toBe('create');
    $gateway->assertDrained();
});

test('healthy active accounts are not sent to the runtime again', function (): void {
    $gateway = new FakeTdlibGateway;
    $this->app->instance(TdlibGateway::class, $gateway);
    $account = auditAccount(AccountLifecycle::Active, [
        'applied_revision' => 1,
    ])->fresh();
    $gateway->queue('listSnapshots', [
        'accounts' => [
            $account->id => [
                'generation' => $account->storage_generation,
                'target_revision' => 1,
                'applied_revision' => 1,
                'authorization_generation' => $account->authorization_generation,
                'effective_config_id' => $account->effective_config_id,
                'runtime_available' => true,
                'operation_id' => null,
                'last_error_code' => null,
            ],
        ],
    ]);
    app(ReconciliationService::class)->run(false);
    expect(collect($gateway->calls)->pluck('method')->all())->toBe(['listSnapshots']);
    $this->assertDatabaseHas('scheduler_statuses', [
        'name' => 'reconciliation',
        'succeeded' => 1,
        'failed' => 0,
    ]);
});

test('a permanently blocked account is counted and can be unblocked', function (): void {
    $account = auditAccount(AccountLifecycle::Provisioning, [
        'reconcile_blocked_revision' => 1,
        'reconcile_failures' => 3,
    ]);
    $instance = Instance::query()->create([]);
    DB::table('scheduler_statuses')->insert([
        'name' => 'reconciliation',
        'last_result' => 'completed',
        'created_at' => now(),
        'updated_at' => now(),
    ]);
    $this->withCredentials()->withCookie('telebezel_owner', auditOwner($instance))->getJson('/v1/owner/settings')
        ->assertOk()->assertJsonPath('data.scheduler.blocked_accounts', 1);
    $this->artisan('telebezel:accounts-unblock', [
        'account' => $account->id,
    ])->assertSuccessful();
    expect($account->fresh()->reconcile_blocked_revision)->toBeNull()
        ->and($account->fresh()->reconcile_failures)->toBe(0);
});

test('a ping answer without latency is a runtime failure, not an invalid request', function (): void {
    auditAccount(AccountLifecycle::Active, [
        'runtime_available' => true,
        'authorization_state' => 'ready',
        'connection_state' => 'ready',
    ]);
    Http::fake(fn (ClientRequest $request) => Http::response([
        'data' => [],
    ]));
    $owner = $this->withCredentials()->withCookie('telebezel_owner', auditOwner(Instance::query()->create([])));
    $owner->postJson('/v1/owner/proxies', [
        'label' => 'No latency',
        'mode' => 'socks5',
        'host' => 'proxy.example',
        'port' => 1080,
    ])->assertCreated()->assertJsonPath('data.ping.ok', false)->assertJsonPath('data.ping.error', 'service.tdlib_unavailable');
});

test('retry-after accepts both header forms and defaults for rate limits', function (): void {
    expect(HttpTdlibGateway::retryAfter('120'))->toBe(120)
        ->and(HttpTdlibGateway::retryAfter(gmdate(DATE_RFC7231, time() + 60)))->toBeGreaterThanOrEqual(59)->toBeLessThanOrEqual(60)
        ->and(HttpTdlibGateway::retryAfter('soon'))->toBeNull();
    config([
        'telebezel.tdlib.base_url' => 'http://tdlib.test',
        'telebezel.tdlib.token' => str_repeat('t', 32),
    ]);
    Http::fake([
        '*' => Http::response([
            'error' => [
                'code' => 'interest.limit_reached',
            ],
        ], 429),
    ]);
    try {
        app(HttpTdlibGateway::class)->updates((string) Str::uuid(), [], (string) Str::uuid());
        $this->fail('The runtime rate limit was not reported.');
    } catch (ApiException $exception) {
        expect($exception->status)->toBe(429)->and($exception->retryAfter)->toBe(5);
    }
});

test('expired sessions codes and idempotency keys are purged', function (): void {
    $instance = Instance::query()->create([]);
    $account = auditAccount();
    DB::table('owner_sessions')->insert([
        'id' => (string) Str::uuid(),
        'instance_id' => $instance->id,
        'token_hash' => str_repeat('a', 64),
        'authenticated_at' => now()->subMonth(),
        'last_interactive_at' => now()->subMonth(),
        'expires_at' => now()->subMonth(),
        'created_at' => now()->subMonth(),
        'updated_at' => now()->subMonth(),
    ]);
    DB::table('bootstrap_codes')->insert([
        'id' => (string) Str::uuid(),
        'code_hash' => str_repeat('b', 64),
        'expires_at' => now()->subWeek(),
        'created_at' => now()->subWeek(),
        'updated_at' => now()->subWeek(),
    ]);
    DB::table('instance_account_idempotency_keys')->insert([
        'instance_id' => $instance->id,
        'key_hash' => str_repeat('c', 64),
        'request_hash' => str_repeat('d', 64),
        'telegram_account_id' => $account->id,
        'created_at' => now()->subMonth(),
        'updated_at' => now()->subMonth(),
    ]);
    $live = auditOwner($instance);
    $this->artisan('telebezel:purge-expired')->assertSuccessful();
    $this->assertDatabaseCount('owner_sessions', 1);
    $this->assertDatabaseHas('owner_sessions', [
        'token_hash' => hash('sha256', $live),
    ]);
    $this->assertDatabaseCount('bootstrap_codes', 0);
    $this->assertDatabaseCount('instance_account_idempotency_keys', 0);
});
