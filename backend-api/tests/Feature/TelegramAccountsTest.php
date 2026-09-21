<?php

use App\Cache\ReconciliationCacheKeys;
use App\Enums\AccountLifecycle;
use App\Models\ApiClient;
use App\Models\TelegramAccount;
use Illuminate\Database\QueryException;
use Illuminate\Http\Client\ConnectionException;
use Illuminate\Support\Facades\Cache;
use Illuminate\Support\Facades\Http;
use Illuminate\Support\Facades\Schema;

beforeEach(function (): void {
    $this->token = 'tb_ccccccccccccccccccccccccccccccccccccccccccc';
    ApiClient::query()->create([
        'name' => 'stage-1-test',
        'token_prefix' => substr($this->token, 0, 12),
        'token_hash' => hash('sha256', $this->token),
    ]);
});
test('creation is durable idempotent and does not store auth inputs', function (): void {
    Http::fake([
        '*' => Http::response([
            'data' => [
                'applied_revision' => 1,
                'runtime_available' => true,
                'authorization_state' => 'awaiting_phone_number',
                'connection_state' => 'waiting_for_network',
            ],
        ]),
    ]);
    $proxyId = '21112233-4455-4677-8899-aabbccddeeff';
    $payload = [
        'label' => 'Primary',
        'proxy' => [
            'id' => $proxyId,
            'mode' => 'http',
            'host' => 'proxy.example',
            'port' => 8080,
            'username' => 'proxy-user',
            'password' => 'proxy-secret-sentinel',
        ],
    ];
    $first = $this->withToken($this->token)->withHeader('Idempotency-Key', 'create-primary-1')->postJson('/v1/telegram/accounts', $payload);
    $first->assertCreated()->assertJsonPath('data.lifecycle', 'active')->assertJsonPath('data.runtime.authorization_state', 'awaiting_phone_number')->assertJsonPath('data.proxy.id', $proxyId)->assertJsonPath('data.proxy.server', 'proxy.example')->assertJsonPath('data.proxy.port', 8080)->assertJsonPath('data.proxy.type', 'http');
    $id = $first->json('data.id');
    $this->withToken($this->token)->withHeader('Idempotency-Key', 'create-primary-1')->postJson('/v1/telegram/accounts', $payload)->assertOk()->assertJsonPath('data.id', $id);
    $this->assertDatabaseCount('telegram_accounts', 1);
    $this->assertDatabaseCount('account_idempotency_keys', 1);
    expect(array_key_exists('phone', TelegramAccount::query()->firstOrFail()->getAttributes()))->toBeFalse();
    expect(Schema::hasColumn('telegram_accounts', 'telegram_identity'))->toBeFalse();
    $this->assertDatabaseHas('telegram_accounts', [
        'id' => $id,
        'proxy_id' => $proxyId,
        'proxy_server' => 'proxy.example',
        'proxy_port' => 8080,
        'proxy_type' => 'http',
    ]);
    foreach (['proxy_mode', 'proxy_host', 'proxy_http_only', 'proxy_username', 'proxy_password', 'proxy_secret'] as $column) {
        expect(Schema::hasColumn('telegram_accounts', $column))->toBeFalse();
    }
});
test('idempotency key rejects a different body', function (): void {
    Http::fake([
        '*' => Http::response([
            'data' => [
                'applied_revision' => 1,
            ],
        ]),
    ]);
    $request = $this->withToken($this->token)->withHeader('Idempotency-Key', 'stable-create-key');
    $request->postJson('/v1/telegram/accounts', [
        'label' => 'One',
    ])->assertCreated();
    $this->withToken($this->token)->withHeader('Idempotency-Key', 'stable-create-key')->postJson('/v1/telegram/accounts', [
        'label' => 'Two',
    ])->assertStatus(409)->assertJsonPath('error.code', 'operation.conflict');
});
test('unknown fields are rejected without echoing values', function (): void {
    $response = $this->withToken($this->token)->withHeader('Idempotency-Key', 'unknown-field-key')->postJson('/v1/telegram/accounts', [
        'label' => 'One',
        'unknown' => 'secret-sentinel',
    ]);
    $response->assertStatus(422)->assertJsonPath('error.code', 'request.invalid');
    expect($response->getContent())->not->toContain('secret-sentinel');
});
test('proxy update uses compare and swap revision', function (): void {
    $account = telegramAccountsTestAccount();
    Http::fake([
        '*' => Http::response([
            'data' => [
                'applied_revision' => 2,
                'runtime_available' => true,
            ],
        ]),
    ]);
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
});
test('proxy update persists desired configuration when tdlib is unavailable', function (): void {
    $account = telegramAccountsTestAccount();
    Http::fake(fn () => throw new ConnectionException('runtime unavailable'));
    $payload = [
        'desired_revision' => 1,
        'id' => '31112233-4455-4677-8899-aabbccddeeff',
        'mode' => 'http',
        'host' => 'proxy.example',
        'port' => 8080,
        'username' => 'proxy-user',
        'password' => 'proxy-secret-sentinel',
    ];
    $this->withToken($this->token)->putJson("/v1/telegram/accounts/{$account->id}/proxy", $payload)->assertStatus(503)->assertJsonPath('error.code', 'service.tdlib_unavailable');
    $this->assertDatabaseHas('telegram_accounts', [
        'id' => $account->id,
        'desired_revision' => 2,
        'proxy_id' => '31112233-4455-4677-8899-aabbccddeeff',
    ]);
});
test('late confirmation cannot acknowledge a newer intent', function (): void {
    Http::fake(function () {
        $account = TelegramAccount::query()->firstOrFail();
        $account->desired_revision = 2;
        $account->operation_id = fake()->uuid();
        $account->save();

        return Http::response([
            'data' => [
                'applied_revision' => 1,
                'runtime_available' => true,
            ],
        ]);
    });
    $response = $this->withToken($this->token)->withHeader('Idempotency-Key', 'late-confirmation-key')->postJson('/v1/telegram/accounts', [
        'label' => 'Racing account',
    ]);
    $response->assertCreated()->assertJsonPath('data.desired_revision', 2)->assertJsonPath('data.applied_revision', null);
    $this->assertDatabaseHas('telegram_accounts', [
        'desired_revision' => 2,
        'applied_revision' => null,
    ]);
});
test('removal requires acknowledgement and known removed ids are gone', function (): void {
    $account = telegramAccountsTestAccount();
    $this->withToken($this->token)->deleteJson("/v1/telegram/accounts/{$account->id}", [])->assertStatus(422)->assertJsonPath('error.code', 'request.invalid');
    Http::fake([
        '*' => Http::response([
            'data' => [
                'applied_revision' => 2,
                'completed' => true,
            ],
        ]),
    ]);
    $this->withToken($this->token)->deleteJson("/v1/telegram/accounts/{$account->id}", [
        'acknowledge_remote_session_remains' => true,
    ])->assertNoContent();
    $this->withToken($this->token)->getJson("/v1/telegram/accounts/{$account->id}")->assertStatus(410)->assertJsonPath('error.code', 'account.gone');
    $this->withToken($this->token)->deleteJson("/v1/telegram/accounts/{$account->id}", [
        'acknowledge_remote_session_remains' => true,
    ])->assertNoContent();
});
test('list survives runtime unavailability and json body is bounded', function (): void {
    telegramAccountsTestAccount();
    Http::fake(fn () => throw new ConnectionException('secret'));
    $this->withToken($this->token)->getJson('/v1/telegram/accounts')->assertOk()->assertJsonCount(1, 'data')->assertJsonPath('data.0.runtime.available', false);
    $this->withToken($this->token)->withHeader('Idempotency-Key', 'oversized-request')->postJson('/v1/telegram/accounts', [
        'label' => str_repeat('x', 17000),
    ])->assertStatus(413)->assertJsonPath('error.code', 'request.body_too_large');
});
test('account list is paginated and fetches runtime snapshots in one batch', function (): void {
    for ($index = 0; $index < 21; $index++) {
        telegramAccountsTestAccount();
    }
    Http::fake([
        '*' => Http::response([
            'data' => [
                'accounts' => [],
            ],
        ]),
    ]);
    $this->withToken($this->token)->getJson('/v1/telegram/accounts?per_page=100')->assertOk()->assertJsonCount(21, 'data')->assertJsonPath('pagination.per_page', 50);
    Http::assertSentCount(1);
    Http::assertSent(function ($request): bool {
        parse_str((string) parse_url($request->url(), PHP_URL_QUERY), $query);

        return count(explode(',', (string) ($query['ids'] ?? ''))) === 21;
    });
});
test('durable removal intent returns accepted when runtime is unavailable', function (): void {
    $account = telegramAccountsTestAccount();
    Http::fake(fn () => throw new ConnectionException('secret'));
    $this->withToken($this->token)->deleteJson("/v1/telegram/accounts/{$account->id}", [
        'acknowledge_remote_session_remains' => true,
    ])->assertAccepted()->assertJsonPath('data.lifecycle', 'removing');
    $this->assertDatabaseHas('telegram_accounts', [
        'id' => $account->id,
        'lifecycle' => 'removing',
    ]);
});
test('reconciliation dry run only reads', function (): void {
    $account = telegramAccountsTestAccount();
    $account->fill([
        'lifecycle' => AccountLifecycle::Removing,
        'desired_revision' => 2,
        'operation_id' => fake()->uuid(),
    ])->save();
    Http::fake([
        '*' => Http::response([
            'data' => [
                'accounts' => [],
            ],
        ]),
    ]);
    $this->artisan('telebezel:accounts-reconcile', [
        '--dry-run' => true,
    ])->assertSuccessful();
    Http::assertSentCount(1);
    Http::assertSent(fn ($request): bool => $request->method() === 'GET');
    $this->assertDatabaseHas('telegram_accounts', [
        'id' => $account->id,
        'lifecycle' => 'removing',
    ]);
});
test('reconciliation resumes removal', function (): void {
    $account = telegramAccountsTestAccount();
    $account->fill([
        'lifecycle' => AccountLifecycle::Removing,
        'desired_revision' => 2,
        'operation_id' => fake()->uuid(),
    ])->save();
    Http::fake([
        '*' => Http::response([
            'data' => [
                'applied_revision' => 2,
                'completed' => true,
            ],
        ]),
    ]);
    $this->artisan('telebezel:accounts-reconcile')->assertSuccessful();
    expect(TelegramAccount::withTrashed()->findOrFail($account->id)->lifecycle)->toBe(AccountLifecycle::Removed);
});
test('reconciliation replays the complete persisted proxy configuration', function (): void {
    $account = telegramAccountsTestAccount();
    $account->fill([
        'lifecycle' => AccountLifecycle::Provisioning,
        'proxy_id' => '31112233-4455-4677-8899-aabbccddeeff',
        'proxy_type' => 'direct',
        'effective_config_id' => fake()->uuid(),
        'operation_id' => fake()->uuid(),
    ])->save();
    Http::fake([
        '*' => Http::response([
            'data' => [
                'applied_revision' => 1,
                'runtime_available' => true,
                'authorization_state' => 'awaiting_phone_number',
            ],
        ]),
    ]);
    $this->artisan('telebezel:accounts-reconcile')->assertSuccessful();
    Http::assertSent(fn ($request): bool => $request->method() === 'PUT' && $request->data()['proxy'] === [
        'id' => $account->proxy_id,
        'mode' => 'direct',
    ]);
    expect($account->fresh()->lifecycle)->toBe(AccountLifecycle::Active);
});
test('reconciliation resumes after the persisted cursor and skips backoff accounts', function (): void {
    foreach (['10112233-4455-4677-8899-aabbccddeeff', '20112233-4455-4677-8899-aabbccddeeff', '30112233-4455-4677-8899-aabbccddeeff'] as $id) {
        TelegramAccount::query()->create([
            'id' => $id,
            'label' => 'Cursor test',
            'storage_generation' => fake()->uuid(),
            'lifecycle' => AccountLifecycle::Active,
            'desired_revision' => 1,
        ]);
    }
    Cache::forever(ReconciliationCacheKeys::cursor(1), '20112233-4455-4677-8899-aabbccddeeff');
    TelegramAccount::query()->whereKey('10112233-4455-4677-8899-aabbccddeeff')->update([
        'next_reconcile_at' => now()->addMinute(),
    ]);
    Http::fake([
        '*' => Http::response([
            'data' => [
                'applied_revision' => 1,
            ],
        ]),
    ]);
    $this->artisan('telebezel:accounts-reconcile')->assertSuccessful();
    $requests = Http::recorded()->map(fn ($pair) => basename(parse_url($pair[0]->url(), PHP_URL_PATH)))->all();
    expect($requests)->toBe(['30112233-4455-4677-8899-aabbccddeeff', '20112233-4455-4677-8899-aabbccddeeff']);
});
test('identity is transient redacted and authorization response is projected', function (): void {
    $account = telegramAccountsTestAccount();
    Http::fake([
        '*' => Http::response([
            'data' => [
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
            ],
        ]),
    ]);
    $shown = $this->withToken($this->token)->getJson("/v1/telegram/accounts/{$account->id}");
    $shown->assertOk()->assertJsonPath('data.telegram_identity.id', '9007199254740000')->assertJsonMissing([
        'phone_number' => '+15550000000',
    ]);
    expect(Schema::hasColumn('telegram_accounts', 'telegram_identity'))->toBeFalse();
    $action = $this->withToken($this->token)->postJson("/v1/telegram/accounts/{$account->id}/authorization/actions", [
        'action' => 'submit_code',
        'authorization_version' => '7',
        'value' => '001234',
    ]);
    $action->assertAccepted()->assertJsonPath('data.state', 'awaiting_code')->assertJsonMissingPath('data.uuid')->assertJsonMissingPath('data.generation');
});
test('proxy read model exposes only its uuid', function (): void {
    $account = telegramAccountsTestAccount();
    $account->proxy_id = '31112233-4455-4677-8899-aabbccddeeff';
    $account->proxy_server = '203.0.113.10';
    $account->proxy_port = 1080;
    $account->proxy_type = 'socks5';
    $account->save();
    $this->withToken($this->token)->getJson("/v1/telegram/accounts/{$account->id}/proxy")->assertOk()->assertJsonPath('data.id', $account->proxy_id)->assertJsonPath('data.server', '203.0.113.10')->assertJsonPath('data.port', 1080)->assertJsonPath('data.type', 'socks5')->assertJsonMissingPath('data.host')->assertJsonMissingPath('data.password');
});
test('database constraint requires scrubbed tombstone', function (): void {
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
});
test('account rate limits use separate shared authorization budgets', function (): void {
    $account = telegramAccountsTestAccount();
    Http::fake([
        '*' => Http::response([
            'data' => [
                'authorization' => [
                    'state' => 'awaiting_code',
                    'authorization_version' => '1',
                    'allowed_actions' => ['submit_code'],
                ],
            ],
        ]),
    ]);
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
});
function telegramAccountsTestAccount(): TelegramAccount
{
    return TelegramAccount::query()->create([
        'label' => 'Test',
        'storage_generation' => fake()->uuid(),
        'lifecycle' => AccountLifecycle::Active,
        'desired_revision' => 1,
    ]);
}
