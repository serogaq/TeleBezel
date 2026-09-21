<?php

use App\Contracts\TdlibGateway;
use App\Exceptions\ApiException;
use App\Models\ApiClient;
use App\Models\Instance;
use App\Models\OwnerSession;
use App\Models\TelegramAccount;
use Illuminate\Support\Facades\Http;
use Illuminate\Support\Str;
use Tests\Integration\TdlibProcess;

beforeEach(function (): void {
    $this->td = new TdlibProcess;
    config([
        'telebezel.tdlib.base_url' => $this->td->url,
        'telebezel.tdlib.token' => 'integration-internal-token',
    ]);
    Http::preventStrayRequests();
    Http::allowStrayRequests([$this->td->url.'/*']);
    $this->token = 'tb_'.str_repeat('a', 43);
    $this->principal = ApiClient::query()->create([
        'name' => 'functional',
        'token_prefix' => substr($this->token, 0, 12),
        'token_hash' => hash('sha256', $this->token),
    ]);
    $this->withToken($this->token);
    $this->view = (string) Str::uuid();
    $this->account = $this->withHeader('Idempotency-Key', 'functional-create')->postJson('/v1/telegram/accounts', [
        'label' => 'Functional',
    ])->assertCreated()->assertJsonPath('data.runtime.available', true)->json('data.id');
    $this->base = '/v1/telegram/accounts/'.$this->account;
    contractWaitState($this, 'awaiting_phone_number');
});

afterEach(function (): void {
    if (isset($this->td)) {
        try {
            expect($this->td->control([
                'op' => 'stats',
            ])['pending'])->toBe(0);
            expect($this->td->logs())->not->toContain('SECRET_TELEGRAM_ERROR', 'integration-internal-token', '+15550000000', '001234');
        } finally {
            $this->td->stop();
        }
    }
});

function contractEventually(Closure $condition): void
{
    for ($attempt = 0; $attempt < 100; $attempt++) {
        if ($condition()) {
            return;
        }
        usleep(10000);
    }
    throw new RuntimeException('Asynchronous TDLib projection did not converge');
}

// Poll the private snapshot so waiting does not consume the public rate budget.
function contractState(object $test, string $state, int $client = 1): void
{
    $test->td->control([
        'op' => 'state',
        'state' => $state,
        'client' => $client,
    ]);
    contractWaitState($test, $state);
}

function contractWaitState(object $test, string $state): void
{
    contractEventually(fn () => app(TdlibGateway::class)->snapshot($test->account, (string) Str::uuid())['authorization_state'] === $state);
}

test('every authorization action crosses the real HTTP contract', function (string $state, string $action, ?string $value, string $function): void {
    contractState($this, $state);
    $auth = $this->getJson($this->base.'/authorization')->assertOk()->json('data');
    expect($auth['allowed_actions'])->toContain($action);
    $payload = [
        'action' => $action,
        'authorization_version' => $auth['authorization_version'],
    ];
    if ($value !== null) {
        $payload['value'] = $value;
    }
    $this->postJson($this->base.'/authorization/actions', $payload)->assertAccepted()->assertJsonPath('data.state', $state);
    expect($this->td->control([
        'op' => 'stats',
    ])['counts'][$function])->toBe(1);
    if ($function === 'code') {
        expect($this->td->control([
            'op' => 'stats',
            'expected_code' => $value,
        ])['code_matches'])->toBeTrue();
    }
    contractState($this, 'ready');
    contractEventually(fn () => isset(app(TdlibGateway::class)->snapshot($this->account, (string) Str::uuid())['telegram_identity']));
    $this->getJson($this->base)->assertOk()->assertJsonPath('data.telegram_identity.id', '9007199254740000')->assertJsonMissingPath('data.telegram_identity.phone_number');
})->with([
    ['awaiting_phone_number', 'submit_phone_number', '+15550000000', 'phone'],
    ['awaiting_phone_number', 'start_qr', null, 'qr'],
    ['awaiting_code', 'submit_code', '001234', 'code'],
    ['awaiting_code', 'resend_code', null, 'resend'],
    ['awaiting_password', 'submit_password', 'secret-password', 'password'],
    ['awaiting_email_address', 'submit_email_address', 'fake@example.test', 'email'],
    ['awaiting_email_code', 'submit_email_code', '001234', 'email_code'],
    ['awaiting_email_code', 'resend_code', null, 'resend_email'],
    ['awaiting_qr_confirmation', 'start_qr', null, 'qr'],
]);

test('Telegram authorization failures are safe and a subsequent retry succeeds', function (string $state, string $action, string $function, int $code, string $message, int $status, string $error): void {
    contractState($this, $state);
    $version = $this->getJson($this->base.'/authorization')->json('data.authorization_version');
    $payload = [
        'action' => $action,
        'authorization_version' => $version,
        'value' => '001234',
    ];
    $this->td->control([
        'op' => 'response',
        'function' => $function,
        'code' => $code,
        'message' => $message.' SECRET_TELEGRAM_ERROR',
    ]);
    $failure = $this->postJson($this->base.'/authorization/actions', $payload)->assertStatus($status)->assertJsonPath('error.code', $error);
    expect($failure->getContent())->not->toContain('SECRET_TELEGRAM_ERROR', '001234');
    $this->postJson($this->base.'/authorization/actions', $payload)->assertAccepted();
})->with([
    ['awaiting_code', 'submit_code', 'code', 400, 'PHONE_CODE_INVALID', 422, 'authorization.invalid_code'],
    ['awaiting_code', 'submit_code', 'code', 400, 'PHONE_CODE_EXPIRED', 422, 'authorization.code_expired'],
    ['awaiting_password', 'submit_password', 'password', 400, 'PASSWORD_HASH_INVALID', 422, 'authorization.invalid_password'],
    ['awaiting_email_code', 'submit_email_code', 'email_code', 400, 'EMAIL_CODE_INVALID', 422, 'authorization.invalid_code'],
    ['awaiting_phone_number', 'submit_phone_number', 'phone', 429, 'FLOOD_WAIT_30', 429, 'authorization.flood_wait'],
    ['awaiting_email_address', 'submit_email_address', 'email', 500, 'SERVER_ERROR', 422, 'telegram.operation_failed'],
]);

test('stale authorization versions and unsupported states never submit credentials', function (): void {
    $old = $this->getJson($this->base.'/authorization')->json('data.authorization_version');
    contractState($this, 'awaiting_code');
    $this->postJson($this->base.'/authorization/actions', [
        'action' => 'submit_code',
        'authorization_version' => $old,
        'value' => '001234',
    ])->assertStatus(409)->assertJsonPath('error.code', 'authorization.invalid_state');
    foreach (['registration_required', 'premium_purchase_required'] as $state) {
        contractState($this, $state);
        $auth = $this->getJson($this->base.'/authorization')->assertOk()->assertJsonPath('data.allowed_actions', [])->json('data');
        $this->postJson($this->base.'/authorization/actions', [
            'action' => 'submit_code',
            'authorization_version' => $auth['authorization_version'],
            'value' => '001234',
        ])->assertStatus(409);
    }
    expect($this->td->control([
        'op' => 'stats',
    ])['counts']['code'])->toBe(0);
});

test('authorization timeout reports unknown outcome and releases the busy flag', function (): void {
    contractState($this, 'awaiting_code');
    $payload = [
        'action' => 'submit_code',
        'authorization_version' => $this->getJson($this->base.'/authorization')->json('data.authorization_version'),
        'value' => '001234',
    ];
    $this->td->control([
        'op' => 'response',
        'function' => 'code',
        'kind' => 'timeout',
    ]);
    $this->postJson($this->base.'/authorization/actions', $payload)->assertStatus(504)->assertJsonPath('error.code', 'operation.outcome_unknown');
    $this->postJson($this->base.'/authorization/actions', $payload)->assertAccepted();
});

test('chat lists paginate by signed string IDs and bind cursors to account and list', function (): void {
    contractState($this, 'ready');
    foreach ([['9007199254740993', 'main', 300], ['42', 'main', 200], ['-100123', 'archive', 100]] as [$id, $list, $order]) {
        $this->td->control([
            'op' => 'chat',
            'id' => $id,
            'list' => $list,
            'order' => $order,
        ]);
    }
    contractEventually(fn () => count(app(TdlibGateway::class)->chats($this->account, [
        'list' => 'main',
        'limit' => 20,
    ], (string) Str::uuid())['items']) === 2);
    $first = $this->getJson($this->base.'/chats?limit=1')->assertOk()->assertJsonPath('data.items.0.id', '9007199254740993')->assertJsonPath('data.has_more', true)->json('data');
    $cursor = urlencode($first['next_cursor']);
    $this->getJson($this->base.'/chats?limit=1&cursor='.$cursor)->assertOk()->assertJsonPath('data.items.0.id', '42')->assertJsonPath('data.has_more', false);
    $this->getJson($this->base.'/chats?list=archive')->assertOk()->assertJsonPath('data.items.0.id', '-100123');
    $this->getJson($this->base.'/chats?list=archive&cursor='.$cursor)->assertStatus(409)->assertJsonPath('error.code', 'cursor.unusable');
    $this->getJson($this->base.'/chats?cursor='.$cursor.'tampered')->assertStatus(409);
    $this->td->control([
        'op' => 'response',
        'function' => 'chats',
    ]);
    $this->getJson($this->base.'/chats')->assertOk()->assertJsonPath('data.partial', true)->assertJsonPath('data.source', 'tdlib_memory')->assertJsonCount(2, 'data.items');
    $second = $this->withHeader('Idempotency-Key', 'functional-second')->postJson('/v1/telegram/accounts', [
        'label' => 'Second',
    ])->assertCreated()->assertJsonPath('data.runtime.available', true)->json('data.id');
    $this->td->control([
        'op' => 'state',
        'state' => 'ready',
        'client' => 2,
    ]);
    contractEventually(fn () => app(TdlibGateway::class)->snapshot($second, (string) Str::uuid())['authorization_state'] === 'ready');
    $this->getJson('/v1/telegram/accounts/'.$second.'/chats?cursor='.$cursor)->assertStatus(409);
    $this->getJson('/v1/telegram/accounts/'.$second.'/chats')->assertOk()->assertJsonCount(0, 'data.items');
});

test('history pagination, retry cursors, local fallback and single message projection', function (): void {
    contractState($this, 'ready');
    $url = $this->base.'/chats/42/messages?view_id='.$this->view.'&limit=2';
    $this->td->control([
        'op' => 'response',
        'function' => 'history',
        'kind' => 'history',
        'items' => [[
            'id' => '9007199254740993',
        ], [
            'id' => '80',
            'unsupported' => true,
        ]],
    ]);
    $page = $this->getJson($url)->assertOk()->assertJsonPath('data.source', 'tdlib')->assertJsonPath('data.items.0.id', '9007199254740993')->assertJsonPath('data.items.0.sender.id', '9007199254740993')->assertJsonPath('data.items.1.content.kind', 'unsupported')->json('data');
    $cursor = urlencode($page['next_cursor']);
    $this->td->control([
        'op' => 'response',
        'function' => 'history',
        'kind' => 'history',
        'items' => [[
            'id' => '80',
        ], [
            'id' => '70',
        ]],
    ]);
    $this->getJson($url.'&cursor='.$cursor)->assertOk()->assertJsonCount(1, 'data.items')->assertJsonPath('data.items.0.id', '70');
    $this->td->control([
        'op' => 'response',
        'function' => 'history',
    ]);
    $this->td->control([
        'op' => 'response',
        'function' => 'history',
        'kind' => 'history',
        'items' => [[
            'id' => '60',
        ]],
    ]);
    $this->getJson($url.'&retry_cursor='.$cursor)->assertOk()->assertJsonPath('data.source', 'tdlib_local')->assertJsonPath('data.partial', true)->assertJsonPath('data.retry_cursor', $page['next_cursor']);
    foreach ([1, 2] as $_) {
        $this->td->control([
            'op' => 'response',
            'function' => 'history',
        ]);
    }
    $this->getJson($url)->assertOk()->assertJsonPath('data.source', 'tdlib_local')->assertJsonCount(2, 'data.items');
    foreach ([1, 2] as $_) {
        $this->td->control([
            'op' => 'response',
            'function' => 'history',
        ]);
    }
    $this->getJson($url.'&cursor='.$cursor)->assertOk()->assertJsonPath('data.items.0.id', '70');
    $this->getJson($url.'&cursor=broken')->assertStatus(409)->assertJsonPath('error.code', 'cursor.unusable');
    $this->td->control([
        'op' => 'response',
        'function' => 'message',
        'kind' => 'message',
        'item' => [
            'id' => '55',
            'text' => 'Fetched',
        ],
    ]);
    $single = $this->base.'/chats/42/messages/55?view_id='.$this->view;
    $this->getJson($single)->assertOk()->assertJsonPath('data.item.content.text', 'Fetched')->assertJsonPath('data.source', 'tdlib');
    $this->getJson($single)->assertOk()->assertJsonPath('data.source', 'tdlib_memory');
    $this->td->control([
        'op' => 'response',
        'function' => 'message',
        'code' => 404,
    ]);
    $this->getJson($this->base.'/chats/42/messages/404?view_id='.$this->view)->assertNotFound()->assertJsonPath('error.code', 'message.not_found');
});

test('chat and message updates can be consumed incrementally without leaking accounts', function (): void {
    contractState($this, 'ready');
    $this->td->control([
        'op' => 'chat',
    ]);
    contractEventually(fn () => isset(app(TdlibGateway::class)->chats($this->account, [
        'list' => 'main',
    ], (string) Str::uuid())['items'][0]));
    $this->getJson($this->base.'/chats/42?view_id='.$this->view)->assertOk()->assertJsonPath('data.item.title', 'Integration chat');
    $cursor = $this->getJson($this->base.'/updates')->assertOk()->json('data.cursor');
    $this->td->control([
        'op' => 'title',
        'title' => 'Changed',
    ]);
    $this->td->control([
        'op' => 'new_message',
        'item' => [
            'id' => '99',
            'text' => 'New',
        ],
    ]);
    $this->td->control([
        'op' => 'delete_message',
        'id' => '99',
    ]);
    contractEventually(fn () => count(app(TdlibGateway::class)->updates($this->account, [
        'cursor' => $cursor,
    ], (string) Str::uuid())['items']) === 3);
    $first = $this->getJson($this->base.'/updates?limit=1&cursor='.urlencode($cursor))->assertOk()->assertJsonCount(1, 'data.items')->assertJsonPath('data.has_more', true)->json('data');
    $this->getJson($this->base.'/updates?cursor='.urlencode($first['cursor']))->assertOk()->assertJsonCount(2, 'data.items')->assertJsonPath('data.has_more', false);
    $this->getJson($this->base.'/updates?cursor=broken')->assertStatus(409)->assertJsonPath('error.code', 'sync.resync_required');
    $this->getJson($this->base.'/chats/42?view_id='.$this->view)->assertOk()->assertJsonPath('data.item.title', 'Changed');
    $this->getJson($this->base.'/chats/404?view_id='.$this->view)->assertNotFound()->assertJsonPath('error.code', 'chat.not_found');
});

test('interest leases coalesce, enforce limits, release principals and roll back failed opens', function (): void {
    contractState($this, 'ready');
    $path = $this->base.'/chats/42/interests/';
    $other = (string) Str::uuid();
    $this->putJson($path.$this->view)->assertOk()->assertJsonPath('data.active', true);
    $this->putJson($path.$this->view)->assertOk();
    $this->putJson($path.$other)->assertOk();
    expect($this->td->control([
        'op' => 'stats',
    ])['counts']['open'])->toBe(1);
    $this->deleteJson($path.$this->view)->assertOk();
    expect($this->td->control([
        'op' => 'stats',
    ])['counts']['close'])->toBe(0);
    $this->deleteJson($path.$other)->assertOk()->assertJsonPath('data.active', false);
    $this->deleteJson($path.$other)->assertOk();
    expect($this->td->control([
        'op' => 'stats',
    ])['counts']['close'])->toBe(1);
    $this->td->control([
        'op' => 'response',
        'function' => 'open',
    ]);
    $this->putJson($path.$this->view)->assertStatus(502)->assertJsonPath('error.code', 'telegram.operation_failed');
    $this->putJson($path.$this->view)->assertOk();
    for ($i = 0; $i < 3; $i++) {
        $this->putJson($path.Str::uuid())->assertOk();
    }
    $this->putJson($path.Str::uuid())->assertStatus(429)->assertJsonPath('error.code', 'interest.limit_reached');
    app(TdlibGateway::class)->releasePrincipalInterests('api_client', $this->principal->id, (string) Str::uuid());
    $this->putJson($path.Str::uuid())->assertOk();
});

test('lifecycle, idempotency, proxy application and tombstones cross both services', function (): void {
    $this->withHeader('Idempotency-Key', 'functional-create')->postJson('/v1/telegram/accounts', [
        'label' => 'Functional',
    ])->assertOk()->assertJsonPath('data.id', $this->account);
    $this->getJson('/v1/telegram/accounts')->assertOk()->assertJsonCount(1, 'data');
    $this->getJson('/v1/status')->assertOk();
    $this->putJson($this->base.'/proxy', [
        'desired_revision' => 1,
        'id' => (string) Str::uuid(),
        'mode' => 'socks5',
        'host' => 'proxy.test',
        'port' => 1080,
    ])->assertAccepted()->assertJsonPath('data.applied_revision', 2);
    $this->getJson($this->base.'/proxy')->assertOk()->assertJsonPath('data.type', 'socks5');
    $proxy = [
        'mode' => 'socks5',
        'host' => 'proxy.test',
        'port' => 1080,
    ];
    expect(app(TdlibGateway::class)->pingProxy($this->account, $proxy, (string) Str::uuid())['latency_ms'])->toBe(125);
    $this->td->control([
        'op' => 'response',
        'function' => 'add_proxy',
    ]);
    $this->putJson($this->base.'/proxy', [
        'desired_revision' => 2,
        'id' => (string) Str::uuid(),
        'mode' => 'http',
        'host' => 'proxy.test',
        'port' => 8080,
    ])->assertStatus(409)->assertJsonPath('error.code', 'configuration.invalid');
    $this->assertDatabaseHas('telegram_accounts', [
        'id' => $this->account,
        'desired_revision' => 3,
    ]);
    $this->postJson($this->base.'/logout')->assertAccepted();
    $this->artisan('telebezel:accounts-reconcile')->assertSuccessful();
    $this->deleteJson($this->base, [
        'acknowledge_remote_session_remains' => true,
    ])->assertNoContent();
    $this->artisan('telebezel:accounts-reconcile')->assertSuccessful();
    $this->getJson($this->base)->assertGone();
    $this->deleteJson($this->base, [
        'acknowledge_remote_session_remains' => true,
    ])->assertNoContent();
    expect(TelegramAccount::withTrashed()->findOrFail($this->account)->label)->toBeNull();
});

test('public validation rejects malformed read requests before sending Telegram commands', function (string $suffix, int $status, string $error): void {
    $before = $this->td->control([
        'op' => 'stats',
    ])['counts'];
    $this->getJson($this->base.str_replace('{view}', $this->view, $suffix))->assertStatus($status)->assertJsonPath('error.code', $error);
    expect($this->td->control([
        'op' => 'stats',
    ])['counts'])->toBe($before);
})->with([
    ['/chats?list=unknown', 422, 'request.invalid'], ['/chats?limit=0', 422, 'request.invalid'], ['/chats?limit=51', 422, 'request.invalid'],
    ['/chats/42', 422, 'request.invalid'], ['/chats/42?view_id=bad', 422, 'request.invalid'],
    ['/chats/9223372036854775808?view_id={view}', 404, 'chat.not_found'],
    ['/chats/42/messages?view_id={view}&cursor=a&retry_cursor=b', 422, 'request.invalid'],
    ['/chats/42/messages?view_id={view}&limit=51', 422, 'request.invalid'],
    ['/chats/42/messages/9223372036854775808?view_id={view}', 404, 'message.not_found'],
    ['/updates?limit=101', 422, 'request.invalid'], ['/chats?unknown=SECRET', 422, 'request.invalid'],
]);

test('the private HTTP contract authenticates every operation and echoes request identity', function (): void {
    $id = (string) Str::uuid();
    $root = '/internal/v1/accounts/'.$this->account;
    foreach ([['GET', '/internal/v1/status'], ['GET', '/internal/v1/accounts'], ['GET', $root], ['PUT', $root], ['DELETE', $root], ['POST', $root.'/logout'], ['PUT', $root.'/proxy'], ['POST', $root.'/proxy/ping'], ['POST', $root.'/authorization/actions'], ['GET', $root.'/chats'], ['GET', $root.'/chats/42'], ['GET', $root.'/chats/42/messages'], ['GET', $root.'/chats/42/messages/99'], ['GET', $root.'/updates'], ['PUT', $root.'/chats/42/interests/'.$this->view], ['DELETE', $root.'/chats/42/interests/'.$this->view], ['DELETE', '/internal/v1/interests/principal']] as [$method, $path]) {
        $response = Http::withToken('wrong-token')->withHeaders([
            'X-Request-ID' => $id,
        ])->send($method, $this->td->url.$path, [
            'json' => [],
        ]);
        expect($response->status())->toBe(401)->and($response->json('error.code'))->toBe('auth.unauthorized')->and($response->header('X-Request-ID'))->toBe($id);
    }
    $this->withHeader('X-Request-ID', $id)->getJson($this->base.'/authorization')->assertOk()->assertHeader('X-Request-ID', $id)->assertJsonPath('request_id', $id);
    expect($this->td->logs())->toContain($id);
    config([
        'telebezel.tdlib.token' => 'wrong-token',
    ]);
    $this->getJson($this->base.'/authorization')->assertStatus(503)->assertJsonPath('error.code', 'service.tdlib_unavailable');
});

test('failed lifecycle commands preserve intent and recover without duplicate revisions', function (string $function, string $method, string $suffix, array $payload): void {
    $this->td->control([
        'op' => 'response',
        'function' => $function,
        'code' => 500,
    ]);
    $this->json($method, $this->base.$suffix, $payload)->assertStatus(502)->assertJsonPath('error.code', 'telegram.operation_failed');
    $account = TelegramAccount::query()->findOrFail($this->account);
    expect($account->desired_revision)->toBe(2)->and($account->operation_id)->not->toBeNull();
    $operation = $account->operation_id;
    $retry = $this->json($method, $this->base.$suffix, $payload);
    if ($function === 'logout') {
        $retry->assertAccepted()->assertJsonPath('data.desired_revision', 2);
        expect($account->fresh()->operation_id)->toBe($operation);
        contractEventually(fn () => app(TdlibGateway::class)->snapshot($this->account, (string) Str::uuid())['authorization_state'] === 'closed');
        $this->artisan('telebezel:accounts-reconcile')->assertSuccessful();
        expect($account->fresh()->logout_completed_at)->not->toBeNull();
    } else {
        $retry->assertNoContent();
        $this->getJson($this->base)->assertGone();
    }
})->with([
    ['logout', 'POST', '/logout', []], [
        'destroy', 'DELETE', '', [
            'acknowledge_remote_session_remains' => true,
        ]],
]);

test('revoking one device closes only its leases and its token stops working', function (): void {
    contractState($this, 'ready');
    $instance = Instance::query()->create([]);
    $ownerToken = 'tbo_'.Str::random(48);
    OwnerSession::query()->create([
        'instance_id' => $instance->id,
        'token_hash' => hash('sha256', $ownerToken),
        'authenticated_at' => now(),
        'last_interactive_at' => now(),
        'expires_at' => now()->addHour(),
    ]);
    $this->withCredentials()->withCookie('telebezel_owner', $ownerToken);
    $first = $this->postJson('/v1/owner/devices', [
        'name' => 'First',
    ])->assertCreated()->json('data');
    $second = $this->postJson('/v1/owner/devices', [
        'name' => 'Second',
    ])->assertCreated()->json('data');
    $lease = $this->base.'/chats/42/interests/'.$this->view;
    $this->withToken($first['token'])->putJson($lease)->assertOk();
    $this->withToken($second['token'])->putJson($lease)->assertOk();
    $this->deleteJson('/v1/owner/devices/'.$first['id'])->assertOk();
    expect($this->td->control([
        'op' => 'stats',
    ])['counts']['close'])->toBe(0);
    $this->withToken($first['token'])->putJson($lease)->assertUnauthorized();
    $this->withToken($second['token'])->putJson($lease)->assertOk();
    $this->deleteJson('/v1/owner/devices/'.$second['id'])->assertOk();
    expect($this->td->control([
        'op' => 'stats',
    ])['counts']['close'])->toBe(1);
});

test('history timeout falls back locally within the public HTTP deadline', function (): void {
    contractState($this, 'ready');
    $this->td->control([
        'op' => 'response',
        'function' => 'history',
        'kind' => 'timeout',
    ]);
    $this->td->control([
        'op' => 'response',
        'function' => 'history',
        'kind' => 'history',
        'items' => [[
            'id' => '99',
        ]],
    ]);
    $started = microtime(true);
    $this->getJson($this->base.'/chats/42/messages?view_id='.$this->view)->assertOk()->assertJsonPath('data.partial', true)->assertJsonPath('data.items.0.id', '99')->assertJsonPath('data.source', 'tdlib_local');
    expect(microtime(true) - $started)->toBeLessThan(10.0);
});

test('a failed proxy ping returns its safe error through the real gateway', function (): void {
    $this->td->control([
        'op' => 'response',
        'function' => 'ping',
        'code' => 500,
    ]);
    try {
        app(TdlibGateway::class)->pingProxy($this->account, [
            'mode' => 'http',
            'host' => 'proxy.test',
            'port' => 8080,
        ], (string) Str::uuid());
        $this->fail('Expected ping failure');
    } catch (ApiException $error) {
        expect($error->errorCode)->toBe('proxy.unreachable')->and($error->status)->toBe(502);
    }
});

test('failed close remains retryable instead of silently leaving a chat open', function (): void {
    contractState($this, 'ready');
    $path = $this->base.'/chats/42/interests/'.$this->view;
    $this->putJson($path)->assertOk();
    $this->td->control([
        'op' => 'response',
        'function' => 'close',
        'code' => 500,
    ]);
    $this->deleteJson($path)->assertStatus(502)->assertJsonPath('error.code', 'telegram.operation_failed');
    $this->deleteJson($path)->assertOk()->assertJsonPath('data.active', false);
    expect($this->td->control([
        'op' => 'stats',
    ])['counts']['close'])->toBe(2);
});
