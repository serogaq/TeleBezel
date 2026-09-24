<?php

use App\Contracts\TdlibGateway;
use App\Enums\AccountLifecycle;
use App\Exceptions\ApiException;
use App\Models\AccessToken;
use App\Models\Instance;
use App\Models\ProxyProfile;
use App\Models\TelegramAccount;
use App\Services\AuthenticationService;
use App\Services\OwnerAccessService;
use App\Services\ProxyProfileService;
use Illuminate\Http\Client\Request as ClientRequest;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Hash;
use Illuminate\Support\Facades\Http;
use Illuminate\Support\Facades\Schema;
use Illuminate\Support\Str;
use Tests\Fakes\FakeTdlibGateway;

beforeEach(function (): void {
    config([
        'app.url' => 'https://telebezel.test',
    ]);
});
test('removed configuration launch flow has no routes or table', function (): void {
    expect(Schema::hasTable('configuration_launches'))->toBeFalse();
    expect(Schema::hasTable('pairing_requests'))->toBeFalse();
    expect(Schema::hasColumn('instances', 'global_proxy'))->toBeFalse();
    $this->postJson('/v1/device/configuration-launches', [
        'state' => str_repeat('l', 24),
    ])->assertNotFound();
    $this->postJson('/v1/configuration-launches/redeem', [
        'ticket' => str_repeat('l', 64),
    ])->assertNotFound();
});
test('owner can bootstrap and receive a persistent session', function (): void {
    $code = 'BOOTSTRAP-TEST-CODE';
    DB::table('bootstrap_codes')->insert([
        'id' => (string) Str::uuid(),
        'code_hash' => hash('sha256', $code),
        'expires_at' => now()->addHour(),
        'created_at' => now(),
        'updated_at' => now(),
    ]);
    $bootstrap = $this->postJson('/v1/session/bootstrap', [
        'bootstrap_code' => $code,
        'password' => 'correct horse battery staple',
    ])->assertCreated();
    $ownerCookie = collect($bootstrap->headers->getCookies())->first(fn ($cookie): bool => $cookie->getName() === 'telebezel_session');
    expect($ownerCookie)->not->toBeNull();
    expect($ownerCookie->getExpiresTime())->toBeGreaterThan(now()->addHours(11)->getTimestamp());
    [, $ownerToken] = $this->app->make(OwnerAccessService::class)->login('correct horse battery staple');
    $bootstrap->assertJsonPath('data.csrf_token', app(AuthenticationService::class)->csrf($ownerCookie->getValue()));
    asWebSession($this, $ownerToken)->getJson('/v1/settings')->assertOk();
    expect(AccessToken::query()->where('type', 'device')->count())->toBe(0);
    $session = AccessToken::query()->where('token_hash', hash('sha256', $ownerCookie->getValue()))->sole();
    expect($session->type->value)->toBe('maintenance')
        ->and($session->idle_timeout_seconds)->toBe(1800)
        ->and($session->claims['permissions'])->not->toContain('messages.read');
});
test('owner configuration is committed before runtime application', function (): void {
    $gateway = new FakeTdlibGateway;
    $gateway->queue('provision', function ($arguments) {
        expect(TelegramAccount::query()->findOrFail($arguments['accountId'])->desired_revision)->toBe(4);
        expect(Instance::query()->firstOrFail()->telegram_api_id)->toBe(12345);
        throw new ApiException('service.tdlib_unavailable', 503);
    });
    $this->app->instance(TdlibGateway::class, $gateway);
    $instance = Instance::query()->create([
        'id' => (string) Str::uuid(),
    ]);
    $account = TelegramAccount::query()->create([
        'id' => (string) Str::uuid(),
        'label' => 'Primary',
        'storage_generation' => (string) Str::uuid(),
        'lifecycle' => AccountLifecycle::Active,
        'desired_revision' => 3,
        'effective_config_id' => (string) Str::uuid(),
    ]);
    $token = issueWebSession()['token'];
    asWebSession($this, $token)->putJson('/v1/settings', [
        'configuration_revision' => 1,
        'telegram_api_id' => 12345,
        'telegram_api_hash' => 'secret-hash',
    ])->assertOk()->assertJsonPath('data.configuration_revision', 2)->assertJsonMissingPath('data.global_proxy');
    expect($account->fresh()->desired_revision)->toBe(4);
    expect($instance->fresh()->telegram_api_id)->toBe(12345);
    expect(DB::table('instances')->where('id', $instance->id)->value('telegram_api_hash'))->not->toBe('secret-hash');
    asWebSession($this, $token)->putJson('/v1/settings', [
        'configuration_revision' => 2,
        'global_proxy' => [
            'mode' => 'direct',
        ],
    ])->assertUnprocessable();
    expect($instance->fresh()->configuration_revision)->toBe(2);
});
test('owner can issue a device token for manual clay setup', function (): void {
    $instance = Instance::query()->create([
        'id' => (string) Str::uuid(),
    ]);
    $ownerToken = issueWebSession()['token'];
    $response = asWebSession($this, $ownerToken)->postJson('/v1/devices', [
        'name' => 'Pebble Manual',
    ])->assertCreated();
    $token = $response->json('data.token');
    expect($token)->toBeString();
    expect($token)->toMatch('/^tb_[A-Za-z0-9_-]{43}$/');
    $this->assertDatabaseHas('access_tokens', [
        'instance_id' => $instance->id,
        'type' => 'device',
        'name' => 'Pebble Manual',
        'token_hash' => hash('sha256', $token),
    ]);
    $this->assertDatabaseMissing('access_tokens', [
        'token_hash' => $token,
    ]);
    $this->withToken($token)->getJson('/v1/device/preferences')->assertOk()->assertJsonPath('data.name', 'Pebble Manual');
    $recovery = asWebSession($this, $ownerToken)->postJson('/v1/session/recovery-code')->assertCreated()->json('data.recovery_code');
    expect($recovery)->toBeString();
    expect(Hash::check($recovery, $instance->fresh()->recovery_code_hash))->toBeTrue();
});
test('owner manages pings and activates proxy profiles', function (): void {
    Http::fake(fn (ClientRequest $request) => str_ends_with($request->url(), '/proxy/ping') ? Http::response([
        'data' => [
            'latency_ms' => 125,
        ],
    ]) : Http::response([
        'data' => [
            'applied_revision' => 2,
            'runtime_available' => true,
            'authorization_state' => 'ready',
            'connection_state' => 'ready',
            'completed' => true,
        ],
    ]));
    $instance = Instance::query()->create([
        'id' => (string) Str::uuid(),
    ]);
    TelegramAccount::query()->create([
        'id' => (string) Str::uuid(),
        'label' => 'Inherited',
        'storage_generation' => (string) Str::uuid(),
        'lifecycle' => AccountLifecycle::Active,
        'desired_revision' => 1,
        'applied_revision' => 1,
        'effective_config_id' => (string) Str::uuid(),
        'runtime_available' => true,
        'authorization_state' => 'ready',
        'connection_state' => 'ready',
    ]);
    $ownerToken = issueWebSession()['token'];
    $owner = asWebSession($this, $ownerToken);
    $profile = $owner->postJson('/v1/proxies', [
        'label' => 'MTProto one',
        'mode' => 'mtproto',
        'host' => 'proxy.example',
        'port' => 443,
        'secret' => 'dd-secret-value',
    ])->assertCreated()->assertJsonPath('data.ping.ok', true)->assertJsonPath('data.ping.latency_ms', 125)->assertJsonMissingPath('data.secret')->json('data');
    $raw = DB::table('proxy_profiles')->where('id', $profile['id'])->value('credentials');
    expect($raw)->toBeString();
    expect($raw)->not->toContain('dd-secret-value');
    $owner->postJson("/v1/proxies/{$profile['id']}/activate")->assertOk()->assertJsonPath('data.0.active', true);
    Http::assertNotSent(fn (ClientRequest $request): bool => str_ends_with($request->url(), '/proxy') && $request->method() === 'PUT');
    expect($instance->fresh()->active_proxy_profile_id)->toBe($profile['id']);
    expect(TelegramAccount::query()->firstOrFail()->desired_revision)->toBe(2);
    $owner->putJson('/v1/proxies/settings', [
        'failure_action' => 'next',
        'connect_timeout_seconds' => 15,
    ])->assertOk();
    $owner->postJson('/v1/proxies/ping')->assertOk()->assertJsonPath('data.0.ping.latency_ms', 125);
    $owner->deleteJson("/v1/proxies/{$profile['id']}")->assertStatus(409)->assertJsonPath('error.code', 'proxy.active');
    $owner->postJson('/v1/proxies/direct')->assertOk();
    $owner->deleteJson("/v1/proxies/{$profile['id']}")->assertNoContent();
});
test('proxy monitor switches to next then direct after timeout', function (): void {
    Http::fake(function (ClientRequest $request) {
        if (str_contains($request->url(), '/internal/v1/accounts?')) {
            return Http::response([
                'data' => [
                    'accounts' => [
                        TelegramAccount::query()->value('id') => [
                            'connection_state' => 'connecting_to_proxy',
                        ],
                    ],
                ],
            ]);
        }

        return Http::response([
            'data' => [
                'applied_revision' => TelegramAccount::query()->value('desired_revision'),
                'runtime_available' => true,
                'authorization_state' => 'ready',
                'connection_state' => 'connecting_to_proxy',
                'completed' => true,
            ],
        ]);
    });
    $instance = Instance::query()->create([
        'id' => (string) Str::uuid(),
        'proxy_failure_action' => 'next',
        'proxy_connect_timeout_seconds' => 10,
    ]);
    $first = ProxyProfile::query()->create([
        'id' => (string) Str::uuid(),
        'instance_id' => $instance->id,
        'label' => 'First',
        'mode' => 'mtproto',
        'host' => 'one.example',
        'port' => 443,
        'credentials' => [
            'secret' => 'first-secret',
        ],
        'position' => 1,
    ]);
    $second = ProxyProfile::query()->create([
        'id' => (string) Str::uuid(),
        'instance_id' => $instance->id,
        'label' => 'Second',
        'mode' => 'mtproto',
        'host' => 'two.example',
        'port' => 443,
        'credentials' => [
            'secret' => 'second-secret',
        ],
        'position' => 2,
    ]);
    $instance->forceFill([
        'active_proxy_profile_id' => $first->id,
        'proxy_failure_started_at' => now()->subSeconds(11),
    ])->save();
    TelegramAccount::query()->create([
        'id' => (string) Str::uuid(),
        'label' => 'Inherited',
        'storage_generation' => (string) Str::uuid(),
        'lifecycle' => AccountLifecycle::Active,
        'desired_revision' => 1,
        'applied_revision' => 1,
        'effective_config_id' => (string) Str::uuid(),
        'runtime_available' => true,
        'authorization_state' => 'ready',
        'connection_state' => 'connecting_to_proxy',
    ]);
    $profiles = $this->app->make(ProxyProfileService::class);
    $profiles->monitor((string) Str::uuid());
    expect($instance->fresh()->active_proxy_profile_id)->toBe($second->id);
    $instance->refresh()->forceFill([
        'proxy_failure_action' => 'direct',
        'proxy_failure_started_at' => now()->subSeconds(11),
    ])->save();
    $profiles->monitor((string) Str::uuid());
    expect($instance->fresh()->active_proxy_profile_id)->toBeNull();
});
test('next policy keeps a failed proxy when no alternative exists', function (): void {
    $instance = Instance::query()->create([
        'proxy_failure_action' => 'next',
        'proxy_connect_timeout_seconds' => 10,
    ]);
    $profile = ProxyProfile::query()->create([
        'instance_id' => $instance->id,
        'label' => 'Only profile',
        'mode' => 'http',
        'host' => 'proxy.example',
        'port' => 8080,
        'position' => 1,
    ]);
    $instance->forceFill([
        'active_proxy_profile_id' => $profile->id,
        'proxy_failure_started_at' => now()->subSeconds(11),
    ])->save();
    $account = TelegramAccount::query()->create([
        'label' => 'Inherited',
        'storage_generation' => (string) Str::uuid(),
        'lifecycle' => AccountLifecycle::Active,
        'desired_revision' => 1,
        'effective_config_id' => (string) Str::uuid(),
    ]);
    Http::fake([
        '*' => Http::response([
            'data' => [
                'accounts' => [
                    $account->id => [
                        'connection_state' => 'connecting_to_proxy',
                    ],
                ],
            ],
        ]),
    ]);
    expect(fn () => $this->app->make(ProxyProfileService::class)->monitor((string) Str::uuid()))
        ->toThrow(ApiException::class, 'proxy.no_alternative');
    expect($instance->fresh()->active_proxy_profile_id)->toBe($profile->id);
});
test('owner can idempotently create a telegram account', function (): void {
    $gateway = new FakeTdlibGateway;
    $gateway->queue('provision', [
        'applied_revision' => 1,
        'runtime_available' => true,
    ]);
    $this->app->instance(TdlibGateway::class, $gateway);
    $instance = Instance::query()->create([
        'id' => (string) Str::uuid(),
    ]);
    $ownerToken = issueWebSession()['token'];
    $proxyId = (string) Str::uuid();
    $request = asWebSession($this, $ownerToken)->withHeader('Idempotency-Key', 'owner-create-primary');
    $first = $request->postJson('/v1/telegram/accounts', [
        'label' => 'Primary',
        'proxy' => [
            'id' => $proxyId,
            'mode' => 'direct',
        ],
    ])->assertAccepted()->assertJsonPath('data.lifecycle', 'provisioning');
    $request->postJson('/v1/telegram/accounts', [
        'label' => 'Primary',
        'proxy' => [
            'id' => $proxyId,
            'mode' => 'direct',
        ],
    ])->assertOk()->assertJsonPath('data.id', $first->json('data.id'));
    $this->assertDatabaseCount('telegram_accounts', 1);
    $this->assertDatabaseHas('account_idempotency_keys', [
        'telegram_account_id' => $first->json('data.id'),
    ]);
    $secondToken = issueWebSession()['token'];
    asWebSession($this, $secondToken)->withHeader('Idempotency-Key', 'owner-create-primary')->postJson('/v1/telegram/accounts', [
        'label' => 'Primary',
        'proxy' => [
            'id' => $proxyId,
            'mode' => 'direct',
        ],
    ])->assertAccepted()->assertJsonMissingPath('data.never');
    $this->assertDatabaseCount('telegram_accounts', 2);
    expect($gateway->calls)->toHaveCount(0);
});
test('only explicit owner activity extends the idle window', function (): void {
    $instance = Instance::query()->create([
        'id' => (string) Str::uuid(),
    ]);
    $session = issueWebSession();
    AccessToken::query()->whereKey($session['id'])->update([
        'last_active_at' => now()->subMinutes(10),
    ]);
    $persistedLastActiveAt = AccessToken::query()->findOrFail($session['id'])->last_active_at;
    $request = asWebSession($this, $session['token']);
    $request->getJson('/v1/settings')->assertOk();
    expect(AccessToken::query()->findOrFail($session['id'])->last_active_at->equalTo($persistedLastActiveAt))->toBeTrue();
    $request->postJson('/v1/session/activity')->assertOk()->assertJsonPath('data.active', true);
    expect(AccessToken::query()->findOrFail($session['id'])->last_active_at->isAfter($persistedLastActiveAt))->toBeTrue();
});
test('an idle or expired web session is revoked and a cookie session needs its csrf value', function (): void {
    $idle = issueWebSession();
    AccessToken::query()->whereKey($idle['id'])->update([
        'last_active_at' => now()->subMinutes(31),
    ]);
    asWebSession($this, $idle['token'])->getJson('/v1/settings')->assertUnauthorized()->assertJsonPath('error.code', 'auth.unauthorized');
    expect(AccessToken::query()->findOrFail($idle['id'])->revoked_at)->not->toBeNull();
    $expired = issueWebSession();
    AccessToken::query()->whereKey($expired['id'])->update([
        'expires_at' => now()->subSecond(),
    ]);
    asWebSession($this, $expired['token'])->getJson('/v1/settings')->assertUnauthorized();
    $session = issueWebSession()['token'];
    $this->withCredentials()->withUnencryptedCookie('telebezel_session', $session)->getJson('/v1/settings')->assertOk();
    $this->withCredentials()->withUnencryptedCookie('telebezel_session', $session)->putJson('/v1/proxies/settings', [
        'failure_action' => 'next',
        'connect_timeout_seconds' => 10,
    ])->assertStatus(419)->assertJsonPath('error.code', 'auth.csrf_mismatch');
    $this->withCredentials()->withUnencryptedCookie('telebezel_session', $session)->withHeader('X-TeleBezel-CSRF', str_repeat('0', 64))->putJson('/v1/proxies/settings', [
        'failure_action' => 'next',
        'connect_timeout_seconds' => 10,
    ])->assertStatus(419);
    Http::fake([
        '*' => Http::response([
            'data' => [
                'released' => 0,
            ],
        ]),
    ]);
    asWebSession($this, $session)->postJson('/v1/session/logout')->assertOk();
    asWebSession($this, $session)->getJson('/v1/settings')->assertUnauthorized();
});
test('removed pairing routes are unavailable', function (): void {
    $this->getJson('/v1/instance')->assertNotFound();
    $this->postJson('/v1/pairings', [])->assertNotFound();
    $this->postJson('/v1/pairings/redeem', [])->assertNotFound();
    $this->postJson('/v1/pairings/'.Str::uuid().'/exchange', [])->assertNotFound();
    $this->postJson('/v1/pairings/'.Str::uuid().'/confirm', [])->assertNotFound();
    $this->postJson('/v1/pairings/'.Str::uuid().'/approve', [])->assertNotFound();
    expect(AccessToken::query()->count())->toBe(0);
});
