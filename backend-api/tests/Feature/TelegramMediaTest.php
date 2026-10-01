<?php

use App\Contracts\RenditionStore;
use App\Enums\AccountLifecycle;
use App\Enums\TokenType;
use App\Models\TelegramAccount;
use App\Services\TelegramAccountService;
use Illuminate\Http\Client\Request;
use Illuminate\Support\Facades\Http;

beforeEach(function (): void {
    $this->cache = sys_get_temp_dir().'/telebezel-media-'.bin2hex(random_bytes(4));
    config()->set('telebezel.media.cache_directory', $this->cache);
    config()->set('telebezel.media.token', str_repeat('m', 64));
    config()->set('telebezel.media.base_url', 'http://media.test');
    config()->set('telebezel.tdlib.base_url', 'http://tdlib.test');
    $this->account = TelegramAccount::query()->create([
        'label' => 'Media',
        'storage_generation' => fake()->uuid(),
        'lifecycle' => AccountLifecycle::Active,
        'desired_revision' => 1,
    ]);
    $this->device = issueTestToken(TokenType::Device)['token'];
    $this->url = '/v1/telegram/accounts/'.$this->account->id.'/chats/42/messages/55/media?width=200&height=228&shape=rect&budget=28672&formats=p4,p2';
    $this->rendition = (string) file_get_contents(base_path('../tests/contracts/media/rect_p4.tbi'));
    $this->tdlibCalls = [];
});

afterEach(function (): void {
    foreach (glob($this->cache.'/*/*') ?: [] as $file) {
        unlink($file);
    }
    foreach (glob($this->cache.'/*') ?: [] as $directory) {
        rmdir($directory);
    }
    @rmdir($this->cache);
});

function mediaDescriptor(string $state, array $extra = []): array
{
    return [
        'state' => $state,
        'retry_after' => $state === 'downloading' ? 1 : null,
        'index' => 0,
        'count' => 3,
        'item_message_id' => '55',
        'has_spoiler' => false,
        'fence' => [
            'storage_generation' => 'g',
            'authorization_generation' => 1,
            'runtime_epoch' => 'epoch',
        ],
        'source' => [
            'kind' => 'photo',
            'width' => 320,
            'height' => 240,
            'size' => 4,
            'unique_id' => 'unique-55',
        ],
        ...$extra,
    ];
}

function fakeMedia(object $test, array $descriptor, mixed $render): void
{
    $test->descriptor = $descriptor;
    $test->render = $render;
    if ($test->faked ?? false) {
        return;
    }
    $test->faked = true;
    Http::fake(function (Request $request) use ($test) {
        if (str_starts_with($request->url(), 'http://tdlib.test')) {
            $test->tdlibCalls[] = $request->url();
            $data = $test->descriptor;
            if (str_contains($request->url(), 'bytes=1')) {
                $data['bytes_base64'] = base64_encode('jpeg');
            }

            return Http::response([
                'data' => $data,
            ]);
        }
        expect($request->header('Authorization')[0])->toBe('Bearer '.str_repeat('m', 64));
        expect($request->body())->toBe('jpeg');

        return $test->render;
    });
}

test('a photo is downloaded, prepared once and then served from the cache', function (): void {
    fakeMedia($this, mediaDescriptor('ready'), Http::response($this->rendition, 200, [
        'Content-Type' => 'application/octet-stream',
    ]));
    $response = $this->withToken($this->device)->getJson($this->url)->assertOk();
    $response->assertJsonPath('data.state', 'ready')->assertJsonPath('data.count', 3)->assertJsonPath('data.item_message_id', '55')
        ->assertJsonPath('data.rendition.width', 200)->assertJsonPath('data.rendition.height', 228)->assertJsonPath('data.rendition.format', 'p4')
        ->assertHeader('Cache-Control', 'no-store, private');
    expect(base64_decode($response->json('data.rendition.bytes_base64')))->toBe($this->rendition);
    expect($response->json('data'))->not->toHaveKeys(['fence', 'source']);
    expect(count($this->tdlibCalls))->toBe(2);
    $this->withToken($this->device)->getJson($this->url)->assertOk()->assertJsonPath('data.state', 'ready');
    expect(count($this->tdlibCalls))->toBe(3);
    expect(array_filter($this->tdlibCalls, fn (string $url): bool => str_contains($url, 'bytes=1')))->toHaveCount(1);
    Http::assertSentCount(4);
});

test('waiting states pass through without calling the media service', function (): void {
    foreach (['downloading', 'spoiler', 'restricted', 'unavailable', 'none'] as $state) {
        fakeMedia($this, mediaDescriptor($state), Http::response('', 500));
        $this->withToken($this->device)->getJson($this->url)->assertOk()->assertJsonPath('data.state', $state)
            ->assertJsonMissingPath('data.rendition');
    }
    Http::assertNotSent(fn (Request $request): bool => str_starts_with($request->url(), 'http://media.test'));
});

test('media service answers become watch states or safe errors', function (): void {
    fakeMedia($this, mediaDescriptor('ready'), Http::response([
        'error' => [
            'code' => 'media.unsupported',
        ],
    ], 422));
    $this->withToken($this->device)->getJson($this->url)->assertOk()->assertJsonPath('data.state', 'unsupported');
    fakeMedia($this, mediaDescriptor('ready'), Http::response([
        'error' => [
            'code' => 'service.busy',
        ],
    ], 503));
    $this->withToken($this->device)->getJson($this->url)->assertOk()->assertJsonPath('data.state', 'preparing')->assertJsonPath('data.retry_after', 1);
    fakeMedia($this, mediaDescriptor('ready'), Http::response('TB not an image', 200));
    $this->withToken($this->device)->getJson($this->url)->assertStatus(503)->assertJsonPath('error.code', 'service.media_unavailable');
    fakeMedia($this, mediaDescriptor('ready'), Http::response([
        'secret' => 'x',
    ], 500));
    $this->withToken($this->device)->getJson($this->url)->assertStatus(503)->assertJsonPath('error.code', 'service.media_unavailable');
});

test('a rendition bigger than the watch budget or for another screen is refused', function (): void {
    fakeMedia($this, mediaDescriptor('ready'), Http::response($this->rendition, 200));
    $tight = str_replace('budget=28672', 'budget=20480', $this->url);
    $this->withToken($this->device)->getJson($tight)->assertStatus(503)->assertJsonPath('error.code', 'service.media_unavailable');
    $round = str_replace('shape=rect', 'shape=round', $this->url);
    $this->withToken($this->device)->getJson($round)->assertStatus(503);
});

test('the media route keeps the read permissions and validates the watch spec', function (): void {
    fakeMedia($this, mediaDescriptor('downloading'), Http::response('', 500));
    $maintenance = issueTestToken(TokenType::Maintenance)['token'];
    $this->withToken($maintenance)->getJson($this->url)->assertForbidden();
    $other = issueTestToken(TokenType::Device, null, [fake()->uuid()])['token'];
    $this->withToken($other)->getJson($this->url)->assertNotFound()->assertJsonPath('error.code', 'account.not_found');
    foreach ([
        'budget=28672' => 'budget=99999',
        'shape=rect' => 'shape=oval',
        'formats=p4,p2' => 'formats=png',
        'width=200' => 'width=4000',
    ] as $from => $to) {
        $this->withToken($this->device)->getJson(str_replace($from, $to, $this->url))->assertStatus(422)->assertJsonPath('error.code', 'request.invalid');
    }
    $this->withToken($this->device)->getJson($this->url.'&path=/etc/passwd')->assertStatus(422);
    $this->withToken($this->device)->getJson(str_replace('/55/', '/../', $this->url))->assertNotFound();
    $this->account->update([
        'lifecycle' => AccountLifecycle::LogoutPending,
    ]);
    $this->withToken($this->device)->getJson($this->url)->assertStatus(409)->assertJsonPath('error.code', 'authorization.invalid_state');
});

test('images have their own rate limit that does not consume reading', function (): void {
    fakeMedia($this, mediaDescriptor('downloading'), Http::response('', 500));
    for ($attempt = 0; $attempt < 40; $attempt++) {
        $this->withToken($this->device)->getJson($this->url)->assertOk();
    }
    $this->withToken($this->device)->getJson($this->url)->assertStatus(429)->assertHeader('Retry-After');
    $this->withToken($this->device)->getJson('/v1/quick-replies')->assertOk();
});

test('logging out or removing an account forgets its renditions', function (): void {
    $store = app(RenditionStore::class);
    $key = str_repeat('a', 64);
    $store->put($this->account->id, $key, 'TB');
    expect($store->get($this->account->id, $key))->toBe('TB');
    expect($store->get($this->account->id, '../../etc/passwd'))->toBeNull();
    $store->forgetAccount($this->account->id);
    expect($store->get($this->account->id, $key))->toBeNull();
    $store->put($this->account->id, $key, 'TB');
    app(TelegramAccountService::class)->logout($this->account->id, 'request');
    expect($store->get($this->account->id, $key))->toBeNull();
});

test('the rendition cache is bounded by size and evicts the least recently used', function (): void {
    config()->set('telebezel.media.cache_bytes', 10);
    $store = app(RenditionStore::class);
    $store->put($this->account->id, str_repeat('1', 64), '123456');
    touch($this->cache.'/'.$this->account->id.'/'.str_repeat('1', 64).'.tbi', time() - 100);
    $store->put($this->account->id, str_repeat('2', 64), '123456');
    expect($store->get($this->account->id, str_repeat('1', 64)))->toBeNull();
    expect($store->get($this->account->id, str_repeat('2', 64)))->toBe('123456');
});
