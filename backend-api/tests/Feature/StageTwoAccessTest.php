<?php

namespace Tests\Feature;

use App\Enums\AccountLifecycle;
use App\Models\Device;
use App\Models\Instance;
use App\Models\OwnerSession;
use App\Models\ProxyProfile;
use App\Models\TelegramAccount;
use App\Services\OwnerAccessService;
use App\Services\ProxyProfileService;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Http\Client\Request as ClientRequest;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Hash;
use Illuminate\Support\Facades\Http;
use Illuminate\Support\Facades\Schema;
use Illuminate\Support\Str;
use Tests\TestCase;

final class StageTwoAccessTest extends TestCase
{
    use RefreshDatabase;

    protected function setUp(): void
    {
        parent::setUp();
        config(['app.url' => 'https://telebezel.test']);
    }

    public function test_removed_configuration_launch_flow_has_no_routes_or_table(): void
    {
        $this->assertFalse(Schema::hasTable('configuration_launches'));
        $this->assertFalse(Schema::hasTable('pairing_requests'));
        $this->assertFalse(Schema::hasColumn('instances', 'global_proxy'));
        $this->postJson('/v1/device/configuration-launches', ['state' => str_repeat('l', 24)])->assertNotFound();
        $this->postJson('/v1/configuration-launches/redeem', ['ticket' => str_repeat('l', 64)])->assertNotFound();
    }

    public function test_owner_can_bootstrap_and_receive_a_persistent_session(): void
    {
        $code = 'BOOTSTRAP-TEST-CODE';
        DB::table('bootstrap_codes')->insert(['id' => (string) Str::uuid(), 'code_hash' => hash('sha256', $code),
            'expires_at' => now()->addHour(), 'created_at' => now(), 'updated_at' => now()]);

        $bootstrap = $this->postJson('/v1/owner/bootstrap', ['bootstrap_code' => $code,
            'password' => 'correct horse battery staple'])->assertCreated();
        $ownerCookie = collect($bootstrap->headers->getCookies())->first(fn ($cookie): bool => $cookie->getName() === 'telebezel_owner');
        $this->assertNotNull($ownerCookie);
        $this->assertGreaterThan(now()->addHours(11)->getTimestamp(), $ownerCookie->getExpiresTime());
        [, $ownerToken] = $this->app->make(OwnerAccessService::class)->login('correct horse battery staple');
        $this->withCredentials()->withCookie('telebezel_owner', $ownerToken)
            ->getJson('/v1/owner/settings')->assertOk();
        $this->assertDatabaseCount('devices', 0);
    }

    public function test_owner_configuration_is_committed_before_runtime_application(): void
    {
        $instance = Instance::query()->create(['id' => (string) Str::uuid()]);
        $account = TelegramAccount::query()->create(['id' => (string) Str::uuid(), 'label' => 'Primary',
            'storage_generation' => (string) Str::uuid(), 'lifecycle' => AccountLifecycle::Active,
            'desired_revision' => 3, 'effective_config_id' => (string) Str::uuid()]);
        $token = 'tbo_'.Str::random(48);
        DB::table('owner_sessions')->insert(['id' => (string) Str::uuid(), 'instance_id' => $instance->id,
            'token_hash' => hash('sha256', $token), 'authenticated_at' => now(), 'last_interactive_at' => now(),
            'expires_at' => now()->addHour(), 'created_at' => now(), 'updated_at' => now()]);

        $this->withCredentials()->withCookie('telebezel_owner', $token)->putJson('/v1/owner/settings', [
            'configuration_revision' => 1, 'telegram_api_id' => 12345, 'telegram_api_hash' => 'secret-hash',
        ])->assertOk()->assertJsonPath('data.configuration_revision', 2)
            ->assertJsonMissingPath('data.global_proxy');

        $this->assertSame(4, $account->fresh()->desired_revision);
        $this->assertSame(12345, $instance->fresh()->telegram_api_id);
        $this->assertNotSame('secret-hash', DB::table('instances')->where('id', $instance->id)->value('telegram_api_hash'));
        $this->withCredentials()->withCookie('telebezel_owner', $token)->putJson('/v1/owner/settings', [
            'configuration_revision' => 2, 'global_proxy' => ['mode' => 'direct'],
        ])->assertUnprocessable();
        $this->assertSame(2, $instance->fresh()->configuration_revision);
    }

    public function test_owner_can_issue_a_device_token_for_manual_clay_setup(): void
    {
        $instance = Instance::query()->create(['id' => (string) Str::uuid()]);
        $ownerToken = 'tbo_'.Str::random(48);
        DB::table('owner_sessions')->insert(['id' => (string) Str::uuid(), 'instance_id' => $instance->id,
            'token_hash' => hash('sha256', $ownerToken), 'authenticated_at' => now(), 'last_interactive_at' => now(),
            'expires_at' => now()->addHour(), 'created_at' => now(), 'updated_at' => now()]);

        $response = $this->withCredentials()->withCookie('telebezel_owner', $ownerToken)
            ->postJson('/v1/owner/devices', ['name' => 'Pebble Manual'])->assertCreated();
        $token = $response->json('data.token');
        $this->assertIsString($token);
        $this->assertMatchesRegularExpression('/^tb_[A-Za-z0-9_-]{43}$/', $token);
        $this->assertDatabaseHas('devices', ['instance_id' => $instance->id, 'name' => 'Pebble Manual',
            'token_hash' => hash('sha256', $token)]);
        $this->assertDatabaseMissing('devices', ['token_hash' => $token]);
        $this->withToken($token)->getJson('/v1/device/preferences')->assertOk()->assertJsonPath('data.name', 'Pebble Manual');

        $recovery = $this->withCredentials()->withCookie('telebezel_owner', $ownerToken)
            ->postJson('/v1/owner/recovery-code')->assertCreated()->json('data.recovery_code');
        $this->assertIsString($recovery);
        $this->assertTrue(Hash::check($recovery, $instance->fresh()->recovery_code_hash));
    }

    public function test_owner_manages_pings_and_activates_proxy_profiles(): void
    {
        Http::fake(fn (ClientRequest $request) => str_ends_with($request->url(), '/proxy/ping')
            ? Http::response(['data' => ['latency_ms' => 125]])
            : Http::response(['data' => ['applied_revision' => 2, 'runtime_available' => true,
                'authorization_state' => 'ready', 'connection_state' => 'ready', 'completed' => true]]));
        $instance = Instance::query()->create(['id' => (string) Str::uuid()]);
        TelegramAccount::query()->create(['id' => (string) Str::uuid(), 'label' => 'Inherited',
            'storage_generation' => (string) Str::uuid(), 'lifecycle' => AccountLifecycle::Active,
            'desired_revision' => 1, 'applied_revision' => 1, 'effective_config_id' => (string) Str::uuid(),
            'runtime_available' => true, 'authorization_state' => 'ready', 'connection_state' => 'ready']);
        $ownerToken = 'tbo_'.Str::random(48);
        DB::table('owner_sessions')->insert(['id' => (string) Str::uuid(), 'instance_id' => $instance->id,
            'token_hash' => hash('sha256', $ownerToken), 'authenticated_at' => now(), 'last_interactive_at' => now(),
            'expires_at' => now()->addHour(), 'created_at' => now(), 'updated_at' => now()]);
        $owner = $this->withCredentials()->withCookie('telebezel_owner', $ownerToken);
        $profile = $owner->postJson('/v1/owner/proxies', ['label' => 'MTProto one', 'mode' => 'mtproto',
            'host' => 'proxy.example', 'port' => 443, 'secret' => 'dd-secret-value'])
            ->assertCreated()->assertJsonPath('data.ping.ok', true)->assertJsonPath('data.ping.latency_ms', 125)
            ->assertJsonMissingPath('data.secret')->json('data');
        $raw = DB::table('proxy_profiles')->where('id', $profile['id'])->value('credentials');
        $this->assertIsString($raw);
        $this->assertStringNotContainsString('dd-secret-value', $raw);

        $owner->postJson("/v1/owner/proxies/{$profile['id']}/activate")->assertOk()
            ->assertJsonPath('data.0.active', true);
        Http::assertSent(fn (ClientRequest $request): bool => str_ends_with($request->url(), '/proxy')
            && $request->method() === 'PUT');
        $this->assertSame($profile['id'], $instance->fresh()->active_proxy_profile_id);
        $this->assertSame(2, TelegramAccount::query()->firstOrFail()->desired_revision);
        $owner->putJson('/v1/owner/proxies/settings', ['failure_action' => 'next', 'connect_timeout_seconds' => 15])
            ->assertOk();
        $owner->postJson('/v1/owner/proxies/ping')->assertOk()->assertJsonPath('data.0.ping.latency_ms', 125);
        $owner->deleteJson("/v1/owner/proxies/{$profile['id']}")->assertStatus(409)
            ->assertJsonPath('error.code', 'proxy.active');
        $owner->postJson('/v1/owner/proxies/direct')->assertOk();
        $owner->deleteJson("/v1/owner/proxies/{$profile['id']}")->assertNoContent();
    }

    public function test_proxy_monitor_switches_to_next_then_direct_after_timeout(): void
    {
        Http::fake(function (ClientRequest $request) {
            if (str_contains($request->url(), '/internal/v1/accounts?')) {
                return Http::response(['data' => ['accounts' => [TelegramAccount::query()->value('id') => [
                    'connection_state' => 'connecting_to_proxy',
                ]]]]);
            }

            return Http::response(['data' => ['applied_revision' => TelegramAccount::query()->value('desired_revision'),
                'runtime_available' => true, 'authorization_state' => 'ready',
                'connection_state' => 'connecting_to_proxy', 'completed' => true]]);
        });
        $instance = Instance::query()->create(['id' => (string) Str::uuid(), 'proxy_failure_action' => 'next',
            'proxy_connect_timeout_seconds' => 10]);
        $first = ProxyProfile::query()->create(['id' => (string) Str::uuid(), 'instance_id' => $instance->id,
            'label' => 'First', 'mode' => 'mtproto', 'host' => 'one.example', 'port' => 443,
            'credentials' => ['secret' => 'first-secret'], 'position' => 1]);
        $second = ProxyProfile::query()->create(['id' => (string) Str::uuid(), 'instance_id' => $instance->id,
            'label' => 'Second', 'mode' => 'mtproto', 'host' => 'two.example', 'port' => 443,
            'credentials' => ['secret' => 'second-secret'], 'position' => 2]);
        $instance->forceFill(['active_proxy_profile_id' => $first->id,
            'proxy_failure_started_at' => now()->subSeconds(11)])->save();
        TelegramAccount::query()->create(['id' => (string) Str::uuid(), 'label' => 'Inherited',
            'storage_generation' => (string) Str::uuid(), 'lifecycle' => AccountLifecycle::Active,
            'desired_revision' => 1, 'applied_revision' => 1, 'effective_config_id' => (string) Str::uuid(),
            'runtime_available' => true, 'authorization_state' => 'ready', 'connection_state' => 'connecting_to_proxy']);

        $profiles = $this->app->make(ProxyProfileService::class);
        $profiles->monitor((string) Str::uuid());
        $this->assertSame($second->id, $instance->fresh()->active_proxy_profile_id);
        $instance->refresh()->forceFill(['proxy_failure_action' => 'direct',
            'proxy_failure_started_at' => now()->subSeconds(11)])->save();
        $profiles->monitor((string) Str::uuid());
        $this->assertNull($instance->fresh()->active_proxy_profile_id);
    }

    public function test_owner_can_idempotently_create_a_telegram_account(): void
    {
        $instance = Instance::query()->create(['id' => (string) Str::uuid()]);
        $ownerSessionId = (string) Str::uuid();
        $ownerToken = 'tbo_'.Str::random(48);
        DB::table('owner_sessions')->insert(['id' => $ownerSessionId, 'instance_id' => $instance->id,
            'token_hash' => hash('sha256', $ownerToken), 'authenticated_at' => now(), 'last_interactive_at' => now(),
            'expires_at' => now()->addHour(), 'created_at' => now(), 'updated_at' => now()]);

        $proxyId = (string) Str::uuid();
        $request = $this->withCredentials()->withCookie('telebezel_owner', $ownerToken)
            ->withHeader('Idempotency-Key', 'owner-create-primary');
        $first = $request->postJson('/v1/owner/telegram/accounts', [
            'label' => 'Primary', 'proxy' => ['id' => $proxyId, 'mode' => 'direct'],
        ])->assertCreated();
        $request->postJson('/v1/owner/telegram/accounts', [
            'label' => 'Primary', 'proxy' => ['id' => $proxyId, 'mode' => 'direct'],
        ])->assertOk()->assertJsonPath('data.id', $first->json('data.id'));

        $this->assertDatabaseCount('telegram_accounts', 1);
        $this->assertDatabaseHas('owner_account_idempotency_keys', ['owner_session_id' => $ownerSessionId,
            'telegram_account_id' => $first->json('data.id')]);
    }

    public function test_only_explicit_owner_activity_extends_the_idle_window(): void
    {
        $instance = Instance::query()->create(['id' => (string) Str::uuid()]);
        $ownerSessionId = (string) Str::uuid();
        $ownerToken = 'tbo_'.Str::random(48);
        $lastInteractiveAt = now()->subMinutes(10);
        DB::table('owner_sessions')->insert(['id' => $ownerSessionId, 'instance_id' => $instance->id,
            'token_hash' => hash('sha256', $ownerToken), 'authenticated_at' => now(),
            'last_interactive_at' => $lastInteractiveAt, 'expires_at' => now()->addHour(),
            'created_at' => now(), 'updated_at' => now()]);
        $persistedLastInteractiveAt = OwnerSession::query()->findOrFail($ownerSessionId)->last_interactive_at;

        $request = $this->withCredentials()->withCookie('telebezel_owner', $ownerToken);
        $request->getJson('/v1/owner/settings')->assertOk();
        $this->assertTrue(
            OwnerSession::query()->findOrFail($ownerSessionId)->last_interactive_at->equalTo($persistedLastInteractiveAt)
        );

        $request->postJson('/v1/owner/activity')->assertOk()->assertJsonPath('data.active', true);
        $this->assertTrue(
            OwnerSession::query()->findOrFail($ownerSessionId)->last_interactive_at->isAfter($persistedLastInteractiveAt)
        );
    }

    public function test_removed_pairing_routes_are_unavailable(): void
    {
        $this->getJson('/v1/instance')->assertNotFound();
        $this->postJson('/v1/pairings', [])->assertNotFound();
        $this->postJson('/v1/pairings/redeem', [])->assertNotFound();
        $this->postJson('/v1/pairings/'.Str::uuid().'/exchange', [])->assertNotFound();
        $this->postJson('/v1/pairings/'.Str::uuid().'/confirm', [])->assertNotFound();
        $this->postJson('/v1/owner/pairings/'.Str::uuid().'/approve', [])->assertNotFound();
        $this->assertSame(0, Device::query()->count());
    }
}
