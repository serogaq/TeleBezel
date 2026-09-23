<?php

use App\Enums\AccountLifecycle;
use App\Models\ApiClient;
use App\Models\TelegramAccount;
use Illuminate\Support\Facades\Http;

// The read contract is what lets a client decide what to do next without
// guessing. Each case below pins one outcome to one combination of fields.
beforeEach(function (): void {
    $this->token = 'tb_ccccccccccccccccccccccccccccccccccccccccccc';
    ApiClient::query()->create([
        'name' => 'read-contract-test',
        'token_prefix' => substr($this->token, 0, 12),
        'token_hash' => hash('sha256', $this->token),
    ]);
    $this->account = TelegramAccount::query()->create([
        'label' => 'Read contract',
        'storage_generation' => fake()->uuid(),
        'lifecycle' => AccountLifecycle::Active,
        'desired_revision' => 1,
        'applied_revision' => 1,
    ]);
    $this->history = '/v1/telegram/accounts/'.$this->account->id.'/chats/42/messages'
        .'?view_id=90112233-4455-4677-8899-aabbccddeeff';
});

function readContractRuntime(array $overrides): array
{
    return [
        'items' => [],
        'stale' => true,
        'partial' => false,
        'has_more' => null,
        'local_exhausted' => false,
        'next_cursor' => null,
        'retry_cursor' => null,
        'updates_cursor' => 'cursor',
        'observed_at' => 1758518400,
        'source' => 'tdlib_local',
        'refresh' => 'queued',
        'fallback_reason' => null,
        'connection' => 'ready',
        ...$overrides,
    ];
}

test('an empty local cache reports a partial page with a retry cursor and a queued refresh', function (): void {
    Http::fake([
        '*' => Http::response([
            'data' => readContractRuntime([
                'partial' => true,
                'retry_cursor' => 'h:1:epoch:42:0.signature',
            ]),
        ]),
    ]);
    $this->withToken($this->token)->getJson($this->history)
        ->assertOk()
        ->assertJsonPath('data.items', [])
        ->assertJsonPath('data.partial', true)
        ->assertJsonPath('data.local_exhausted', false)
        ->assertJsonPath('data.has_more', null)
        ->assertJsonPath('data.next_cursor', null)
        ->assertJsonPath('data.retry_cursor', 'h:1:epoch:42:0.signature')
        ->assertJsonPath('data.refresh', 'queued');
});

test('the start of history is a complete page that states it will not continue', function (): void {
    Http::fake([
        '*' => Http::response([
            'data' => readContractRuntime([
                'items' => [[
                    'id' => '10',
                    'chat_id' => '42',
                    'date' => 1,
                    'content' => [
                        'kind' => 'text',
                        'text' => 'first',
                    ],
                ]],
                'local_exhausted' => true,
                'has_more' => false,
                'refresh' => 'unchanged',
            ]),
        ]),
    ]);
    $this->withToken($this->token)->getJson($this->history)
        ->assertOk()
        ->assertJsonPath('data.local_exhausted', true)
        ->assertJsonPath('data.has_more', false)
        ->assertJsonPath('data.partial', false)
        ->assertJsonPath('data.next_cursor', null)
        ->assertJsonPath('data.retry_cursor', null)
        ->assertJsonPath('data.refresh', 'unchanged');
});

test('an anchor-only page advertises no progress and keeps the same retry cursor', function (): void {
    Http::fake([
        '*' => Http::response([
            'data' => readContractRuntime([
                'partial' => true,
                'retry_cursor' => 'h:1:epoch:42:100.signature',
                'refresh' => 'queued',
            ]),
        ]),
    ]);
    $this->withToken($this->token)->getJson($this->history.'&cursor=h:1:epoch:42:100.signature')
        ->assertOk()
        ->assertJsonPath('data.next_cursor', null)
        ->assertJsonPath('data.retry_cursor', 'h:1:epoch:42:100.signature')
        ->assertJsonPath('data.partial', true);
});

test('a short page that can still continue is partial and carries a next cursor', function (): void {
    Http::fake([
        '*' => Http::response([
            'data' => readContractRuntime([
                'items' => [[
                    'id' => '99',
                    'chat_id' => '42',
                    'date' => 2,
                    'content' => [
                        'kind' => 'text',
                        'text' => 'only one',
                    ],
                ]],
                'partial' => true,
                'has_more' => true,
                'next_cursor' => 'h:1:epoch:42:99.signature',
                'retry_cursor' => 'h:1:epoch:42:100.signature',
            ]),
        ]),
    ]);
    $this->withToken($this->token)->getJson($this->history.'&cursor=h:1:epoch:42:100.signature')
        ->assertOk()
        ->assertJsonPath('data.has_more', true)
        ->assertJsonPath('data.next_cursor', 'h:1:epoch:42:99.signature')
        ->assertJsonPath('data.retry_cursor', 'h:1:epoch:42:100.signature')
        ->assertJsonPath('data.partial', true);
});

test('a read deadline is reported as a partial page with a reason, not as an empty history', function (): void {
    Http::fake([
        '*' => Http::response([
            'data' => readContractRuntime([
                'partial' => true,
                'fallback_reason' => 'read.deadline',
                'retry_cursor' => 'h:1:epoch:42:100.signature',
            ]),
        ]),
    ]);
    $this->withToken($this->token)->getJson($this->history.'&cursor=h:1:epoch:42:100.signature')
        ->assertOk()
        ->assertJsonPath('data.partial', true)
        ->assertJsonPath('data.local_exhausted', false)
        ->assertJsonPath('data.fallback_reason', 'read.deadline')
        ->assertJsonPath('data.retry_cursor', 'h:1:epoch:42:100.signature');
});

test('a saturated refresh queue still serves the page and says the refresh was refused', function (): void {
    Http::fake([
        '*' => Http::response([
            'data' => readContractRuntime([
                'items' => [[
                    'id' => '99',
                    'chat_id' => '42',
                    'date' => 2,
                    'content' => [
                        'kind' => 'photo',
                        'fallback_key' => 'message.photo',
                    ],
                ]],
                'local_exhausted' => true,
                'has_more' => false,
                'refresh' => 'saturated',
            ]),
        ]),
    ]);
    $this->withToken($this->token)->getJson($this->history)
        ->assertOk()
        ->assertJsonPath('data.refresh', 'saturated')
        ->assertJsonPath('data.partial', false);
});

test('a cursor from a session that no longer exists is refused with resync, not with an empty page', function (): void {
    Http::fake([
        '*' => Http::response([
            'error' => [
                'code' => 'sync.resync_required',
            ],
        ], 409),
    ]);
    $this->withToken($this->token)->getJson($this->history.'&cursor=h:1:epoch:42:100.signature')
        ->assertStatus(409)
        ->assertJsonPath('error.code', 'sync.resync_required');
});

test('the chat list reports exhaustion and pagination through the same fields', function (): void {
    Http::fake([
        '*' => Http::response([
            'data' => readContractRuntime([
                'items' => [],
                'local_exhausted' => true,
                'has_more' => false,
                'source' => 'tdlib_memory',
            ]),
        ]),
    ]);
    $this->withToken($this->token)->getJson('/v1/telegram/accounts/'.$this->account->id.'/chats?list=main')
        ->assertOk()
        ->assertJsonPath('data.has_more', false)
        ->assertJsonPath('data.local_exhausted', true)
        ->assertJsonPath('data.partial', false)
        ->assertJsonPath('data.next_cursor', null);
});

test('message content keeps captions and descriptive fields and strips internal ones', function (): void {
    Http::fake([
        '*' => Http::response([
            'data' => readContractRuntime([
                'items' => [[
                    'id' => '99',
                    'chat_id' => '42',
                    'date' => 2,
                    'content' => [
                        'kind' => 'voice_note',
                        'fallback_key' => 'message.voice_note',
                        'text' => 'Caption',
                        'duration' => 14,
                        'emoji' => 'x',
                        'title' => 'Title',
                        'action' => 'pinned',
                        'preview_file_id' => 7,
                        'preview_mime' => 'image/jpeg',
                    ],
                ]],
                'local_exhausted' => true,
                'has_more' => false,
            ]),
        ]),
    ]);
    $content = $this->withToken($this->token)->getJson($this->history)
        ->assertOk()
        ->json('data.items.0.content');
    expect($content)->toBe([
        'kind' => 'voice_note',
        'fallback_key' => 'message.voice_note',
        'text' => 'Caption',
        'duration' => 14,
        'emoji' => 'x',
        'title' => 'Title',
        'action' => 'pinned',
    ]);
});

test('updates report the connection and whether the account goes through a proxy', function (): void {
    Http::fake([
        '*' => Http::response([
            'data' => [
                'items' => [],
                'cursor' => 'next',
                'has_more' => false,
                'connection' => 'connecting_to_proxy',
            ],
        ]),
    ]);
    $updates = '/v1/telegram/accounts/'.$this->account->id.'/updates';
    $this->withToken($this->token)->getJson($updates)
        ->assertOk()
        ->assertJsonPath('data.status', [
            'connection' => 'connecting_to_proxy',
            'proxy' => false,
        ]);
    $this->account->forceFill([
        'proxy_id' => fake()->uuid(),
        'proxy_type' => 'socks5',
        'proxy_server' => 'proxy.example',
        'proxy_port' => 1080,
        'proxy_config' => [
            'mode' => 'socks5',
            'host' => 'proxy.example',
            'port' => 1080,
        ],
        'proxy_config_version' => 1,
    ])->save();
    $this->withToken($this->token)->getJson($updates)
        ->assertOk()
        ->assertJsonPath('data.status.proxy', true);
    $this->account->forceFill([
        'proxy_type' => 'direct',
        'proxy_server' => null,
        'proxy_port' => null,
        'proxy_config' => [
            'mode' => 'direct',
        ],
        'proxy_config_version' => 2,
    ])->save();
    $this->withToken($this->token)->getJson($updates)
        ->assertOk()
        ->assertJsonPath('data.status.proxy', false);
});

test('chat pages keep unmuted unread counters and the saved messages flag', function (): void {
    Http::fake([
        '*' => Http::response([
            'data' => readContractRuntime([
                'items' => [[
                    'id' => '7',
                    'type' => 'private',
                    'title' => 'Ada',
                    'is_saved_messages' => true,
                    'internal' => 'hidden',
                ]],
                'unread' => [
                    'chats' => 2,
                    'messages' => 5,
                    'muted' => 9,
                ],
            ]),
        ]),
    ]);
    $this->withToken($this->token)->getJson('/v1/telegram/accounts/'.$this->account->id.'/chats')
        ->assertOk()
        ->assertJsonPath('data.unread', [
            'chats' => 2,
            'messages' => 5,
        ])
        ->assertJsonPath('data.items.0.is_saved_messages', true)
        ->assertJsonMissingPath('data.items.0.internal');
});
