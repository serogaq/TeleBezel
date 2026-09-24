<?php

use App\Cache\RateLimitCacheKeys;
use App\Contracts\TdlibGateway;
use App\Enums\AccountLifecycle;
use App\Enums\TokenType;
use App\Exceptions\ApiException;
use App\Models\AccessToken;
use App\Models\MessageSend;
use App\Models\TelegramAccount;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\RateLimiter;
use Illuminate\Support\Str;
use Tests\Fakes\FakeTdlibGateway;

beforeEach(function (): void {
    config([
        'cache.default' => 'array',
    ]);
    $this->gateway = new FakeTdlibGateway;
    $this->app->instance(TdlibGateway::class, $this->gateway);
    $this->device = issueTestToken(TokenType::Device);
    $this->account = TelegramAccount::query()->create([
        'label' => 'Sender',
        'storage_generation' => (string) Str::uuid(),
        'lifecycle' => AccountLifecycle::Active,
        'desired_revision' => 1,
        'applied_revision' => 1,
        'effective_config_id' => (string) Str::uuid(),
    ]);
    $this->url = '/v1/telegram/accounts/'.$this->account->id.'/chats/42/messages';
    RateLimiter::clear(RateLimitCacheKeys::accountOperation('send', $this->account->id));
});

function sentOperation(array $overrides = []): Closure
{
    return fn (array $arguments): array => [
        'operation_id' => $arguments['command']['operation_id'],
        'state' => 'sent',
        'chat_id' => '42',
        'message_id' => '900',
        'retryable' => false,
        'reply_dropped' => false,
        'error' => null,
        ...$overrides,
    ];
}

test('a send is dispatched once per key and repeats return the recorded operation', function (): void {
    $this->gateway->queue('sendMessage', sentOperation());
    $first = $this->withToken($this->device['token'])->withHeader('Idempotency-Key', 'watch-send-0001')->postJson($this->url, [
        'text' => "  Hello\r\nwatch\u{0007}  ",
        'reply_to_message_id' => '55',
    ])->assertStatus(202)->assertJsonPath('data.operation.state', 'sent')->assertJsonPath('data.operation.message_id', '900')->assertJsonPath('data.operation.reply_to_message_id', '55');
    $call = $this->gateway->calls[0]['arguments'];
    expect($call['command']['text'])->toBe("Hello\nwatch")
        ->and($call['command']['reply_to_message_id'])->toBe('55')
        ->and($call['command']['operation_id'])->toBe($first->json('data.operation.id'))
        ->and($call['command']['storage_generation'])->toBe($this->account->storage_generation);
    $this->withToken($this->device['token'])->withHeader('Idempotency-Key', 'watch-send-0001')->postJson($this->url, [
        'text' => "Hello\nwatch",
        'reply_to_message_id' => '55',
    ])->assertStatus(202)->assertJsonPath('data.operation.id', $first->json('data.operation.id'));
    expect($this->gateway->calls)->toHaveCount(1);
    $row = DB::table('message_sends')->sole();
    expect(json_encode($row))->not->toContain('Hello')
        ->and($row->request_hmac)->not->toBe(hash('sha256', "Hello\nwatch"));
});

test('the same key with a different body is a conflict and another token gets its own operation', function (): void {
    $this->gateway->queue('sendMessage', sentOperation())->queue('sendMessage', sentOperation([
        'message_id' => '901',
    ]));
    $this->withToken($this->device['token'])->withHeader('Idempotency-Key', 'watch-send-0002')->postJson($this->url, [
        'text' => 'First',
    ])->assertStatus(202);
    $this->withToken($this->device['token'])->withHeader('Idempotency-Key', 'watch-send-0002')->postJson($this->url, [
        'text' => 'Second',
    ])->assertStatus(409)->assertJsonPath('error.code', 'operation.conflict');
    $other = issueTestToken(TokenType::Device)['token'];
    $this->withToken($other)->withHeader('Idempotency-Key', 'watch-send-0002')->postJson($this->url, [
        'text' => 'First',
    ])->assertStatus(202)->assertJsonPath('data.operation.message_id', '901');
    expect(MessageSend::query()->count())->toBe(2);
});

test('an unknown outcome is never dispatched again and a repeat only asks for the status', function (): void {
    $this->gateway->queue('sendMessage', new ApiException('operation.outcome_unknown', 504));
    $first = $this->withToken($this->device['token'])->withHeader('Idempotency-Key', 'watch-send-0003')->postJson($this->url, [
        'text' => 'Maybe',
    ])->assertStatus(202)->assertJsonPath('data.operation.state', 'unknown')->assertJsonPath('data.operation.retryable', false);
    $id = $first->json('data.operation.id');
    $this->gateway->queue('sendStatus', fn (array $arguments): array => [
        'operations' => [
            $id => [
                'operation_id' => $id,
                'state' => 'sent',
                'message_id' => '902',
                'retryable' => false,
                'reply_dropped' => false,
                'error' => null,
            ],
        ],
    ]);
    $this->withToken($this->device['token'])->withHeader('Idempotency-Key', 'watch-send-0003')->postJson($this->url, [
        'text' => 'Maybe',
    ])->assertStatus(202)->assertJsonPath('data.operation.state', 'sent')->assertJsonPath('data.operation.message_id', '902');
    expect(collect($this->gateway->calls)->where('method', 'sendMessage'))->toHaveCount(1);
});

test('a runtime that forgot a pending operation turns it into unknown only after a grace period', function (): void {
    $this->gateway->queue('sendMessage', fn (array $arguments): array => [
        'operation_id' => $arguments['command']['operation_id'],
        'state' => 'pending',
        'chat_id' => '42',
        'temporary_message_id' => '1000000',
        'retryable' => false,
        'reply_dropped' => false,
        'error' => null,
    ]);
    $id = $this->withToken($this->device['token'])->withHeader('Idempotency-Key', 'watch-send-0004')->postJson($this->url, [
        'text' => 'Slow',
    ])->assertStatus(202)->assertJsonPath('data.operation.state', 'pending')->json('data.operation.id');
    $forgotten = fn (): array => [
        'operations' => [
            $id => [
                'operation_id' => $id,
                'state' => 'not_found',
            ],
        ],
    ];
    $this->gateway->queue('sendStatus', $forgotten)->queue('sendStatus', $forgotten);
    $status = '/v1/telegram/accounts/'.$this->account->id.'/sends/'.$id;
    $this->withToken($this->device['token'])->getJson($status)->assertOk()->assertJsonPath('data.operation.state', 'pending');
    $this->travel(20)->seconds();
    $this->withToken($this->device['token'])->getJson($status)->assertOk()->assertJsonPath('data.operation.state', 'unknown')->assertJsonPath('data.operation.error.code', 'operation.outcome_unknown');
    $this->withToken(issueTestToken(TokenType::Device)['token'])->getJson($status)->assertNotFound()->assertJsonPath('error.code', 'operation.not_found');
});

test('failures that happened before Telegram saw the message are retryable failures', function (): void {
    $this->gateway->queue('sendMessage', new ApiException('service.busy', 503, 1));
    $this->withToken($this->device['token'])->withHeader('Idempotency-Key', 'watch-send-0005')->postJson($this->url, [
        'text' => 'Busy',
    ])->assertStatus(202)->assertJsonPath('data.operation.state', 'failed')->assertJsonPath('data.operation.error.code', 'service.busy')->assertJsonPath('data.operation.retryable', true);
    $this->gateway->queue('sendMessage', fn (array $arguments): array => [
        'operation_id' => $arguments['command']['operation_id'],
        'state' => 'failed',
        'chat_id' => '42',
        'retryable' => true,
        'reply_dropped' => false,
        'error' => [
            'code' => 'message.send_rate_limited',
            'retry_after' => 30,
        ],
    ]);
    $this->withToken($this->device['token'])->withHeader('Idempotency-Key', 'watch-send-0006')->postJson($this->url, [
        'text' => 'Flood',
    ])->assertStatus(202)->assertJsonPath('data.operation.error.code', 'message.send_rate_limited')->assertJsonPath('data.operation.error.retry_after', 30);
});

test('text, reply target and idempotency key are validated before anything is stored', function (): void {
    $send = fn (array $body, string $key = 'watch-send-0007') => $this->withToken($this->device['token'])->withHeader('Idempotency-Key', $key)->postJson($this->url, $body);
    $send([
        'text' => " \n\u{0007} ",
    ])->assertStatus(422)->assertJsonPath('error.code', 'request.invalid');
    $send([
        'text' => str_repeat('a', 4095).'😀',
    ])->assertStatus(422)->assertJsonPath('error.code', 'message.text_too_long');
    $send([
        'text' => 'ok',
        'reply_to_message_id' => '0',
    ])->assertStatus(422);
    $send([
        'text' => 'ok',
    ], 'short')->assertStatus(422)->assertJsonPath('error.code', 'request.invalid_idempotency_key');
    $send([
        'text' => 'ok',
        'extra' => true,
    ])->assertStatus(422);
    expect(MessageSend::query()->count())->toBe(0);
    expect($this->gateway->calls)->toBe([]);
});

test('only device tokens with the send permission on an active account may send', function (): void {
    $maintenance = issueTestToken(TokenType::Maintenance)['token'];
    $this->withToken($maintenance)->withHeader('Idempotency-Key', 'watch-send-0008')->postJson($this->url, [
        'text' => 'nope',
    ])->assertForbidden();
    $reader = issueTestToken(TokenType::Device, ['messages.read'])['token'];
    $this->withToken($reader)->withHeader('Idempotency-Key', 'watch-send-0008')->postJson($this->url, [
        'text' => 'nope',
    ])->assertForbidden()->assertJsonPath('error.code', 'auth.insufficient_scope');
    $elsewhere = issueTestToken(TokenType::Device, accounts: [(string) Str::uuid()])['token'];
    $this->withToken($elsewhere)->withHeader('Idempotency-Key', 'watch-send-0008')->postJson($this->url, [
        'text' => 'nope',
    ])->assertNotFound()->assertJsonPath('error.code', 'account.not_found');
    AccessToken::query()->whereKey($this->device['id'])->update([
        'revoked_at' => now(),
    ]);
    $this->withToken($this->device['token'])->withHeader('Idempotency-Key', 'watch-send-0008')->postJson($this->url, [
        'text' => 'nope',
    ])->assertUnauthorized();
    $active = issueTestToken(TokenType::Device)['token'];
    $this->account->forceFill([
        'lifecycle' => AccountLifecycle::LogoutPending,
    ])->save();
    $this->withToken($active)->withHeader('Idempotency-Key', 'watch-send-0008')->postJson($this->url, [
        'text' => 'nope',
    ])->assertStatus(409)->assertJsonPath('error.code', 'authorization.invalid_state');
    expect($this->gateway->calls)->toBe([]);
});

test('sending is rate limited per account with a send specific code', function (): void {
    for ($attempt = 0; $attempt < 20; $attempt++) {
        $this->gateway->queue('sendMessage', sentOperation());
        $this->withToken($this->device['token'])->withHeader('Idempotency-Key', 'watch-send-limit-'.$attempt)->postJson($this->url, [
            'text' => 'n'.$attempt,
        ])->assertStatus(202);
    }
    $this->withToken($this->device['token'])->withHeader('Idempotency-Key', 'watch-send-limit-x')->postJson($this->url, [
        'text' => 'over',
    ])->assertStatus(429)->assertJsonPath('error.code', 'message.send_rate_limited')->assertHeader('Retry-After');
});

test('updates deliver send events only to the token that owns the operation and record every outcome', function (): void {
    $this->gateway->queue('sendMessage', fn (array $arguments): array => [
        'operation_id' => $arguments['command']['operation_id'],
        'state' => 'pending',
        'chat_id' => '42',
        'retryable' => false,
        'reply_dropped' => false,
        'error' => null,
    ]);
    $mine = $this->withToken($this->device['token'])->withHeader('Idempotency-Key', 'watch-send-0009')->postJson($this->url, [
        'text' => 'Mine',
    ])->json('data.operation.id');
    $otherToken = issueTestToken(TokenType::Device);
    $this->gateway->queue('sendMessage', fn (array $arguments): array => [
        'operation_id' => $arguments['command']['operation_id'],
        'state' => 'pending',
        'chat_id' => '42',
        'retryable' => false,
        'reply_dropped' => false,
        'error' => null,
    ]);
    $theirs = $this->withToken($otherToken['token'])->withHeader('Idempotency-Key', 'watch-send-0010')->postJson($this->url, [
        'text' => 'Theirs',
    ])->json('data.operation.id');
    $events = [
        [
            'sequence' => 1,
            'type' => 'message_changed',
            'chat_id' => '42',
            'message_id' => '900',
        ],
        [
            'sequence' => 2,
            'type' => 'send_changed',
            'operation_id' => $mine,
            'chat_id' => '42',
            'message_id' => '900',
            'state' => 'sent',
            'retryable' => false,
            'reply_dropped' => true,
            'error' => null,
        ],
        [
            'sequence' => 3,
            'type' => 'send_changed',
            'operation_id' => $theirs,
            'chat_id' => '42',
            'state' => 'failed',
            'retryable' => false,
            'reply_dropped' => false,
            'error' => [
                'code' => 'message.send_forbidden',
                'secret' => 'x',
            ],
        ],
        [
            'sequence' => 4,
            'type' => 'send_changed',
            'operation_id' => (string) Str::uuid(),
            'chat_id' => '42',
            'state' => 'sent',
        ],
        [
            'sequence' => 5,
            'type' => 'connection_changed',
            'connection' => 'connecting',
        ],
    ];
    $this->gateway->queue('updates', [
        'items' => $events,
        'cursor' => 'next',
        'has_more' => false,
        'connection' => 'connecting',
    ])->queue('updates', [
        'items' => $events,
        'cursor' => 'next',
        'has_more' => false,
        'connection' => 'connecting',
    ]);
    $updates = '/v1/telegram/accounts/'.$this->account->id.'/updates';
    $all = $this->withToken($this->device['token'])->getJson($updates.'?cursor=start')->assertOk()->json('data');
    expect(array_column($all['events'], 'sequence'))->toBe([1, 2, 5])
        ->and($all['events'][1]['reply_dropped'])->toBeTrue()
        ->and($all['cursor'])->toBe('next')
        ->and($all)->not->toHaveKey('items');
    expect(MessageSend::query()->findOrFail($mine)->getAttribute('state'))->toBe('sent')
        ->and(MessageSend::query()->findOrFail($theirs)->getAttribute('error_code'))->toBe('message.send_forbidden');
    $filtered = $this->withToken($this->device['token'])->getJson($updates.'?cursor=start&types=send,connection')->assertOk()->json('data.events');
    expect(array_column($filtered, 'type'))->toBe(['send_changed', 'connection_changed']);
    $this->withToken($this->device['token'])->getJson($updates.'?wait=5')->assertStatus(422);
    $this->withToken($this->device['token'])->getJson($updates.'?types=everything')->assertStatus(422);
});

test('send records are purged after the idempotency window', function (): void {
    $this->gateway->queue('sendMessage', sentOperation());
    $this->withToken($this->device['token'])->withHeader('Idempotency-Key', 'watch-send-0011')->postJson($this->url, [
        'text' => 'Old',
    ])->assertStatus(202);
    MessageSend::query()->update([
        'created_at' => now()->subDays(8),
    ]);
    $this->artisan('telebezel:purge-expired')->assertSuccessful();
    expect(MessageSend::query()->count())->toBe(0);
});
