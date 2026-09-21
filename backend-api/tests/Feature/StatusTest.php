<?php

use App\Cache\RateLimitCacheKeys;
use App\Models\ApiClient;
use Illuminate\Database\QueryException;
use Illuminate\Http\Client\ConnectionException;
use Illuminate\Support\Facades\Config;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Http;
use Illuminate\Support\Facades\RateLimiter;

beforeEach(function (): void {
    $this->token = 'tb_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
    ApiClient::query()->create([
        'name' => 'test',
        'token_prefix' => substr($this->token, 0, 12),
        'token_hash' => hash('sha256', $this->token),
    ]);
    RateLimiter::clear(RateLimitCacheKeys::publicApi('1'));
});
test('status requires a valid token', function (): void {
    $this->getJson('/v1/status')->assertStatus(401)->assertJsonPath('error.code', 'auth.unauthorized');
    $this->withToken('tb_bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb')->getJson('/v1/status')->assertStatus(401);
});
test('status returns the stable ready dto', function (): void {
    $payload = tdlibReadyStatusFixture();
    Http::fake([
        '*' => Http::response($payload),
    ]);
    $response = $this->withToken($this->token)->getJson('/v1/status');
    $response->assertOk()->assertHeader('Cache-Control')->assertJsonPath('data.status', 'ready')->assertJsonPath('data.dependencies.postgres', 'ready')->assertJsonPath('data.dependencies.tdlib', 'ready')->assertJsonPath('data.api_version', config()->string('telebezel.version'))->assertJsonPath('data.tdlib_version', $payload['data']['tdlib_version'])->assertJsonStructure(['request_id']);
    expect((string) $response->headers->get('Cache-Control'))->toContain('no-store');
});
test('revoked token is rejected', function (): void {
    ApiClient::query()->update([
        'revoked_at' => now(),
    ]);
    $this->withToken($this->token)->getJson('/v1/status')->assertStatus(401);
});
test('tdlib connection failure is redacted', function (): void {
    Http::fake(fn () => throw new ConnectionException('contains-sensitive-upstream-detail'));
    $response = $this->withToken($this->token)->getJson('/v1/status');
    $response->assertStatus(503)->assertJsonPath('error.code', 'service.tdlib_unavailable');
    expect($response->getContent())->not->toContain('sensitive');
});
test('tdlib malformed response is unavailable', function (): void {
    Http::fake([
        '*' => Http::response([
            'raw' => 'tdlib',
        ]),
    ]);
    $this->withToken($this->token)->getJson('/v1/status')->assertStatus(503)->assertJsonPath('error.code', 'service.tdlib_unavailable');
});
test('request state does not leak between requests', function (): void {
    Http::fake([
        '*' => Http::response(tdlibReadyStatusFixture()),
    ]);
    $first = $this->withToken($this->token)->getJson('/v1/status');
    $second = $this->withToken($this->token)->getJson('/v1/status');
    expect($second->json('request_id'))->not->toBe($first->json('request_id'));
});
test('authenticated status is rate limited per client', function (): void {
    Http::fake([
        '*' => Http::response(tdlibReadyStatusFixture()),
    ]);
    for ($attempt = 0; $attempt < 60; $attempt++) {
        $this->withToken($this->token)->getJson('/v1/status')->assertOk();
    }
    $this->withToken($this->token)->getJson('/v1/status')->assertStatus(429)->assertHeader('Retry-After')->assertJsonPath('error.code', 'rate_limit.exceeded');
});
test('authentication reports database unavailability', function (): void {
    $original = Config::get('database.connections.pgsql');
    DB::purge('pgsql');
    Config::set('database.connections.pgsql.port', 1);
    try {
        $this->withToken($this->token)->getJson('/v1/status')->assertStatus(503)->assertJsonPath('error.code', 'service.database_unavailable');
    } finally {
        Config::set('database.connections.pgsql', $original);
        DB::purge('pgsql');
    }
});
test('contradictory tdlib readiness is rejected', function (): void {
    $payload = tdlibReadyStatusFixture();
    $payload['data']['client_manager'] = 'not_ready';
    Http::fake([
        '*' => Http::response($payload),
    ]);
    $this->withToken($this->token)->get('/v1/status')->assertStatus(503)->assertJsonPath('error.code', 'service.tdlib_unavailable');
});
test('invalid tdlib contract variants are redacted', function (): void {
    $variants = [];
    foreach (['service_version', 'client_manager', 'tdlib_version', 'accounts'] as $missing) {
        $variant = tdlibReadyStatusFixture();
        unset($variant['data'][$missing]);
        $variants[] = $variant;
    }
    foreach ([['tdlib_version', ''], ['accounts', -1], ['accounts', '0'], ['service_version', '01.0.0']] as [$key, $value]) {
        $variant = tdlibReadyStatusFixture();
        $variant['data'][$key] = $value;
        $variants[] = $variant;
    }
    foreach ($variants as $variant) {
        Http::fake([
            '*' => Http::response($variant),
        ]);
        $this->withToken($this->token)->get('/v1/status')->assertStatus(503)->assertJsonPath('error.code', 'service.tdlib_unavailable');
    }
    Http::fake([
        '*' => Http::response([
            'error' => [
                'code' => 'secret-upstream',
            ],
        ], 401),
    ]);
    $response = $this->withToken($this->token)->get('/v1/status');
    $response->assertStatus(503)->assertJsonPath('error.code', 'service.tdlib_unavailable');
    expect($response->getContent())->not->toContain('secret-upstream');
});
test('rate limiter database failures keep the public json contract', function (string $stage): void {
    $failure = new QueryException('pgsql', 'select secret', [], new RuntimeException('proxy-secret-sentinel'));
    if ($stage === 'tooManyAttempts') {
        RateLimiter::shouldReceive('tooManyAttempts')->once()->andThrow($failure);
    } elseif ($stage === 'hit') {
        RateLimiter::shouldReceive('tooManyAttempts')->once()->andReturn(false);
        RateLimiter::shouldReceive('hit')->once()->andThrow($failure);
    } else {
        RateLimiter::shouldReceive('tooManyAttempts')->once()->andReturn(true);
        RateLimiter::shouldReceive('availableIn')->once()->andThrow($failure);
    }
    $response = $this->withToken($this->token)->get('/v1/status');
    $response->assertStatus(503)->assertJsonPath('error.code', 'service.database_unavailable')->assertJsonStructure(['request_id']);
    expect((string) $response->headers->get('Content-Type'))->toContain('application/json');
    expect((string) $response->headers->get('Cache-Control'))->toContain('no-store');
    expect($response->getContent())->not->toContain('sentinel');
})->with([
    'read' => ['tooManyAttempts'],
    'increment' => ['hit'],
    'retry-after' => ['availableIn'],
]);
