<?php

namespace Tests\Feature;

use App\Enums\AccountLifecycle;
use App\Models\ApiClient;
use App\Models\TelegramAccount;
use Illuminate\Database\QueryException;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Http\Client\ConnectionException;
use Illuminate\Support\Facades\Cache;
use Illuminate\Support\Facades\Http;
use Illuminate\Support\Facades\Schema;
use Tests\TestCase;

final class TelegramAccountsTest extends TestCase
{
    use RefreshDatabase;

    private string $token = 'tb_ccccccccccccccccccccccccccccccccccccccccccc';

    protected function setUp(): void
    {
        parent::setUp();
        ApiClient::query()->create([
            'name' => 'stage-1-test',
            'token_prefix' => substr($this->token, 0, 12),
            'token_hash' => hash('sha256', $this->token),
        ]);
    }

    public function test_creation_is_durable_idempotent_and_does_not_store_auth_inputs(): void
    {
        Http::fake(['*' => Http::response(['data' => [
            'applied_revision' => 1,
            'runtime_available' => true,
            'authorization_state' => 'awaiting_phone_number',
            'connection_state' => 'waiting_for_network',
        ]])]);
        $proxyId = '21112233-4455-4677-8899-aabbccddeeff';
        $payload = ['label' => 'Primary', 'proxy' => [
            'id' => $proxyId,
            'mode' => 'http',
            'host' => 'proxy.example',
            'port' => 8080,
            'username' => 'proxy-user',
            'password' => 'proxy-secret-sentinel',
        ]];
        $first = $this->withToken($this->token)->withHeader('Idempotency-Key', 'create-primary-1')
            ->postJson('/v1/telegram/accounts', $payload);
        $first->assertCreated()->assertJsonPath('data.lifecycle', 'active')
            ->assertJsonPath('data.runtime.authorization_state', 'awaiting_phone_number')
            ->assertJsonPath('data.proxy.id', $proxyId)
            ->assertJsonPath('data.proxy.server', 'proxy.example')
            ->assertJsonPath('data.proxy.port', 8080)
            ->assertJsonPath('data.proxy.type', 'http');
        $id = $first->json('data.id');

        $this->withToken($this->token)->withHeader('Idempotency-Key', 'create-primary-1')
            ->postJson('/v1/telegram/accounts', $payload)->assertOk()->assertJsonPath('data.id', $id);
        $this->assertDatabaseCount('telegram_accounts', 1);
        $this->assertDatabaseCount('account_idempotency_keys', 1);
        $this->assertFalse(array_key_exists('phone', TelegramAccount::query()->firstOrFail()->getAttributes()));
        $this->assertFalse(Schema::hasColumn('telegram_accounts', 'telegram_identity'));
        $this->assertDatabaseHas('telegram_accounts', [
            'id' => $id,
            'proxy_id' => $proxyId,
            'proxy_server' => 'proxy.example',
            'proxy_port' => 8080,
            'proxy_type' => 'http',
        ]);
        foreach (['proxy_mode', 'proxy_host', 'proxy_http_only', 'proxy_username', 'proxy_password', 'proxy_secret'] as $column) {
            $this->assertFalse(Schema::hasColumn('telegram_accounts', $column));
        }
    }

    public function test_idempotency_key_rejects_a_different_body(): void
    {
        Http::fake(['*' => Http::response(['data' => ['applied_revision' => 1]])]);
        $request = $this->withToken($this->token)->withHeader('Idempotency-Key', 'stable-create-key');
        $request->postJson('/v1/telegram/accounts', ['label' => 'One'])->assertCreated();
        $this->withToken($this->token)->withHeader('Idempotency-Key', 'stable-create-key')
            ->postJson('/v1/telegram/accounts', ['label' => 'Two'])
            ->assertStatus(409)->assertJsonPath('error.code', 'operation.conflict');
    }

    public function test_unknown_fields_are_rejected_without_echoing_values(): void
    {
        $response = $this->withToken($this->token)->withHeader('Idempotency-Key', 'unknown-field-key')
            ->postJson('/v1/telegram/accounts', ['label' => 'One', 'unknown' => 'secret-sentinel']);
        $response->assertStatus(422)->assertJsonPath('error.code', 'request.invalid');
        $this->assertStringNotContainsString('secret-sentinel', $response->getContent());
    }

    public function test_proxy_update_uses_compare_and_swap_revision(): void
    {
        $account = $this->account();
        Http::fake(['*' => Http::response(['data' => ['applied_revision' => 2, 'runtime_available' => true]])]);
        $this->withToken($this->token)->putJson("/v1/telegram/accounts/{$account->id}/proxy", [
            'desired_revision' => 1,
            'id' => '31112233-4455-4677-8899-aabbccddeeff',
            'mode' => 'http',
            'host' => 'proxy.example',
            'port' => 8080,
            'http_only' => true,
        ])->assertAccepted()->assertJsonPath('data.desired_revision', 2);
        $this->withToken($this->token)->putJson("/v1/telegram/accounts/{$account->id}/proxy", [
            'desired_revision' => 1,
            'id' => '41112233-4455-4677-8899-aabbccddeeff',
            'mode' => 'direct',
        ])->assertStatus(409)->assertJsonPath('error.code', 'operation.conflict');
    }

    public function test_proxy_update_keeps_database_revision_unchanged_until_tdlib_has_the_full_configuration(): void
    {
        $account = $this->account();
        $attempt = 0;
        Http::fake(function ($request) use (&$attempt) {
            $attempt++;
            if ($attempt === 1) {
                throw new ConnectionException('runtime unavailable');
            }

            return Http::response(['data' => ['applied_revision' => 2, 'completed' => true, 'runtime_available' => true]]);
        });
        $payload = [
            'desired_revision' => 1,
            'id' => '31112233-4455-4677-8899-aabbccddeeff',
            'mode' => 'http',
            'host' => 'proxy.example',
            'port' => 8080,
            'username' => 'proxy-user',
            'password' => 'proxy-secret-sentinel',
        ];

        $this->withToken($this->token)->putJson("/v1/telegram/accounts/{$account->id}/proxy", $payload)
            ->assertStatus(503)->assertJsonPath('error.code', 'service.tdlib_unavailable');
        $this->assertDatabaseHas('telegram_accounts', [
            'id' => $account->id,
            'desired_revision' => 1,
            'proxy_id' => null,
        ]);

        $this->withToken($this->token)->putJson("/v1/telegram/accounts/{$account->id}/proxy", $payload)
            ->assertAccepted()->assertJsonPath('data.desired_revision', 2);
        Http::assertSent(fn ($request): bool => $request->method() === 'PUT'
            && data_get($request->data(), 'proxy.password') === 'proxy-secret-sentinel');
    }

    public function test_late_confirmation_cannot_acknowledge_a_newer_intent(): void
    {
        Http::fake(function () {
            $account = TelegramAccount::query()->firstOrFail();
            $account->desired_revision = 2;
            $account->operation_id = fake()->uuid();
            $account->save();

            return Http::response(['data' => ['applied_revision' => 1, 'runtime_available' => true]]);
        });

        $response = $this->withToken($this->token)->withHeader('Idempotency-Key', 'late-confirmation-key')
            ->postJson('/v1/telegram/accounts', ['label' => 'Racing account']);

        $response->assertCreated()->assertJsonPath('data.desired_revision', 2)
            ->assertJsonPath('data.applied_revision', null);
        $this->assertDatabaseHas('telegram_accounts', ['desired_revision' => 2, 'applied_revision' => null]);
    }

    public function test_removal_requires_acknowledgement_and_known_removed_ids_are_gone(): void
    {
        $account = $this->account();
        $this->withToken($this->token)->deleteJson("/v1/telegram/accounts/{$account->id}", [])
            ->assertStatus(422)->assertJsonPath('error.code', 'request.invalid');
        Http::fake(['*' => Http::response(['data' => ['applied_revision' => 2, 'completed' => true]])]);
        $this->withToken($this->token)->deleteJson("/v1/telegram/accounts/{$account->id}", [
            'acknowledge_remote_session_remains' => true,
        ])->assertNoContent();
        $this->withToken($this->token)->getJson("/v1/telegram/accounts/{$account->id}")
            ->assertStatus(410)->assertJsonPath('error.code', 'account.gone');
        $this->withToken($this->token)->deleteJson("/v1/telegram/accounts/{$account->id}", [
            'acknowledge_remote_session_remains' => true,
        ])->assertNoContent();
    }

    public function test_list_survives_runtime_unavailability_and_json_body_is_bounded(): void
    {
        $this->account();
        Http::fake(fn () => throw new ConnectionException('secret'));
        $this->withToken($this->token)->getJson('/v1/telegram/accounts')
            ->assertOk()->assertJsonCount(1, 'data')->assertJsonPath('data.0.runtime.available', false);
        $this->withToken($this->token)->withHeader('Idempotency-Key', 'oversized-request')
            ->postJson('/v1/telegram/accounts', ['label' => str_repeat('x', 17000)])
            ->assertStatus(413)->assertJsonPath('error.code', 'request.body_too_large');
    }

    public function test_account_list_is_paginated_and_fetches_runtime_snapshots_in_one_batch(): void
    {
        for ($index = 0; $index < 21; $index++) {
            $this->account();
        }
        Http::fake(['*' => Http::response(['data' => ['accounts' => []]])]);

        $this->withToken($this->token)->getJson('/v1/telegram/accounts?per_page=100')
            ->assertOk()->assertJsonCount(21, 'data')->assertJsonPath('pagination.per_page', 50);
        Http::assertSentCount(1);
        Http::assertSent(function ($request): bool {
            parse_str((string) parse_url($request->url(), PHP_URL_QUERY), $query);

            return count(explode(',', (string) ($query['ids'] ?? ''))) === 21;
        });
    }

    public function test_durable_removal_intent_returns_accepted_when_runtime_is_unavailable(): void
    {
        $account = $this->account();
        Http::fake(fn () => throw new ConnectionException('secret'));
        $this->withToken($this->token)->deleteJson("/v1/telegram/accounts/{$account->id}", [
            'acknowledge_remote_session_remains' => true,
        ])->assertAccepted()->assertJsonPath('data.lifecycle', 'removing');
        $this->assertDatabaseHas('telegram_accounts', ['id' => $account->id, 'lifecycle' => 'removing']);
    }

    public function test_reconciliation_dry_run_only_reads(): void
    {
        $account = $this->account();
        $account->fill([
            'lifecycle' => AccountLifecycle::Removing,
            'desired_revision' => 2,
            'operation_id' => fake()->uuid(),
        ])->save();
        Http::fake(['*' => Http::response(['data' => ['accounts' => []]])]);

        $this->artisan('telebezel:accounts-reconcile', ['--dry-run' => true])->assertSuccessful();
        Http::assertSentCount(1);
        Http::assertSent(fn ($request): bool => $request->method() === 'GET');
        $this->assertDatabaseHas('telegram_accounts', ['id' => $account->id, 'lifecycle' => 'removing']);
    }

    public function test_reconciliation_resumes_removal(): void
    {
        $account = $this->account();
        $account->fill([
            'lifecycle' => AccountLifecycle::Removing,
            'desired_revision' => 2,
            'operation_id' => fake()->uuid(),
        ])->save();
        Http::fake(['*' => Http::response(['data' => ['applied_revision' => 2, 'completed' => true]])]);
        $this->artisan('telebezel:accounts-reconcile')->assertSuccessful();
        $this->assertSame(AccountLifecycle::Removed, TelegramAccount::withTrashed()->findOrFail($account->id)->lifecycle);
    }

    public function test_reconciliation_resumes_after_the_persisted_cursor_and_skips_backoff_accounts(): void
    {
        foreach ([
            '10112233-4455-4677-8899-aabbccddeeff',
            '20112233-4455-4677-8899-aabbccddeeff',
            '30112233-4455-4677-8899-aabbccddeeff',
        ] as $id) {
            TelegramAccount::query()->create([
                'id' => $id,
                'label' => 'Cursor test',
                'storage_generation' => fake()->uuid(),
                'lifecycle' => AccountLifecycle::Active,
                'desired_revision' => 1,
            ]);
        }
        Cache::forever('telebezel:accounts-reconcile:cursor:1', '20112233-4455-4677-8899-aabbccddeeff');
        Cache::put('telebezel:accounts-reconcile:backoff:10112233-4455-4677-8899-aabbccddeeff', true, now()->addMinute());
        Http::fake(['*' => Http::response(['data' => ['applied_revision' => 1]])]);

        $this->artisan('telebezel:accounts-reconcile')->assertSuccessful();

        $requests = Http::recorded()->map(fn ($pair) => basename(parse_url($pair[0]->url(), PHP_URL_PATH)))->all();
        $this->assertSame([
            '30112233-4455-4677-8899-aabbccddeeff',
            '20112233-4455-4677-8899-aabbccddeeff',
        ], $requests);
    }

    public function test_identity_is_transient_redacted_and_authorization_response_is_projected(): void
    {
        $account = $this->account();
        Http::fake(['*' => Http::response(['data' => [
            'runtime_available' => true,
            'telegram_identity' => [
                'id' => '9007199254740000',
                'first_name' => 'Ada',
                'last_name' => 'Lovelace',
                'usernames' => ['ada'],
                'is_premium' => true,
                'phone_number' => '+15550000000',
            ],
            'authorization' => [
                'state' => 'awaiting_code',
                'authorization_version' => '7',
                'allowed_actions' => ['submit_code'],
            ],
            'uuid' => $account->id,
            'generation' => $account->storage_generation,
        ]])]);

        $shown = $this->withToken($this->token)->getJson("/v1/telegram/accounts/{$account->id}");
        $shown->assertOk()->assertJsonPath('data.telegram_identity.id', '9007199254740000')
            ->assertJsonMissing(['phone_number' => '+15550000000']);
        $this->assertFalse(Schema::hasColumn('telegram_accounts', 'telegram_identity'));

        $action = $this->withToken($this->token)->postJson("/v1/telegram/accounts/{$account->id}/authorization/actions", [
            'action' => 'submit_code',
            'authorization_version' => '7',
            'value' => '001234',
        ]);
        $action->assertAccepted()->assertJsonPath('data.state', 'awaiting_code')
            ->assertJsonMissingPath('data.uuid')->assertJsonMissingPath('data.generation');
    }

    public function test_proxy_read_model_exposes_only_its_uuid(): void
    {
        $account = $this->account();
        $account->proxy_id = '31112233-4455-4677-8899-aabbccddeeff';
        $account->proxy_server = '203.0.113.10';
        $account->proxy_port = 1080;
        $account->proxy_type = 'socks5';
        $account->save();

        $this->withToken($this->token)->getJson("/v1/telegram/accounts/{$account->id}/proxy")
            ->assertOk()->assertJsonPath('data.id', $account->proxy_id)
            ->assertJsonPath('data.server', '203.0.113.10')
            ->assertJsonPath('data.port', 1080)
            ->assertJsonPath('data.type', 'socks5')
            ->assertJsonMissingPath('data.host')->assertJsonMissingPath('data.password');
    }

    public function test_database_constraint_requires_scrubbed_tombstone(): void
    {
        $this->expectException(QueryException::class);
        TelegramAccount::query()->create([
            'label' => 'Must be scrubbed',
            'storage_generation' => fake()->uuid(),
            'lifecycle' => AccountLifecycle::Removed,
            'desired_revision' => 1,
            'proxy_id' => fake()->uuid(),
            'proxy_server' => 'proxy.example',
            'proxy_port' => 8080,
            'proxy_type' => 'http',
        ]);
    }

    public function test_account_rate_limits_use_separate_shared_authorization_budgets(): void
    {
        $account = $this->account();
        Http::fake(['*' => Http::response(['data' => ['authorization' => [
            'state' => 'awaiting_code',
            'authorization_version' => '1',
            'allowed_actions' => ['submit_code'],
        ]]])]);
        $url = "/v1/telegram/accounts/{$account->id}/authorization/actions";

        for ($attempt = 0; $attempt < 3; $attempt++) {
            $this->withToken($this->token)->postJson($url, [
                'action' => 'submit_phone_number',
                'authorization_version' => '1',
                'value' => '+15550000000',
            ])->assertAccepted();
        }
        $secondToken = 'tb_ddddddddddddddddddddddddddddddddddddddddddd';
        ApiClient::query()->create([
            'name' => 'stage-1-second-test',
            'token_prefix' => substr($secondToken, 0, 12),
            'token_hash' => hash('sha256', $secondToken),
        ]);
        $this->withToken($secondToken)->postJson($url, [
            'action' => 'submit_phone_number',
            'authorization_version' => '1',
            'value' => '+15550000000',
        ])->assertStatus(429);

        for ($attempt = 0; $attempt < 10; $attempt++) {
            $this->withToken($this->token)->postJson($url, [
                'action' => 'submit_code',
                'authorization_version' => '1',
                'value' => '001234',
            ])->assertAccepted();
        }
        $this->withToken($this->token)->postJson($url, [
            'action' => 'submit_code',
            'authorization_version' => '1',
            'value' => '001234',
        ])->assertStatus(429)->assertHeader('Retry-After');
    }

    private function account(): TelegramAccount
    {
        return TelegramAccount::query()->create([
            'label' => 'Test',
            'storage_generation' => fake()->uuid(),
            'lifecycle' => AccountLifecycle::Active,
            'desired_revision' => 1,
        ]);
    }
}
