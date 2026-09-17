<?php

namespace Tests\Feature;

use App\Models\ApiClient;
use Illuminate\Database\QueryException;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Http\Client\ConnectionException;
use Illuminate\Support\Facades\Config;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Http;
use Illuminate\Support\Facades\RateLimiter;
use PHPUnit\Framework\Attributes\DataProvider;
use Tests\TestCase;

final class StatusTest extends TestCase
{
    use RefreshDatabase;

    private string $token = 'tb_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';

    protected function setUp(): void
    {
        parent::setUp();
        ApiClient::query()->create(['name' => 'test', 'token_prefix' => substr($this->token, 0, 12), 'token_hash' => hash('sha256', $this->token)]);
        RateLimiter::clear('public-api:1');
    }

    public function test_status_requires_a_valid_token(): void
    {
        $this->getJson('/v1/status')->assertStatus(401)->assertJsonPath('error.code', 'auth.unauthorized');
        $this->withToken('tb_bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb')->getJson('/v1/status')->assertStatus(401);
    }

    public function test_status_returns_the_stable_ready_dto(): void
    {
        Http::fake(['*' => Http::response($this->readyTdlib())]);
        $response = $this->withToken($this->token)->getJson('/v1/status');
        $response->assertOk()->assertHeader('Cache-Control')
            ->assertJsonPath('data.status', 'ready')->assertJsonPath('data.dependencies.postgres', 'ready')
            ->assertJsonPath('data.dependencies.tdlib', 'ready')->assertJsonPath('data.tdlib_version', '1.8.67')
            ->assertJsonStructure(['request_id']);
        $this->assertStringContainsString('no-store', (string) $response->headers->get('Cache-Control'));
    }

    public function test_revoked_token_is_rejected(): void
    {
        ApiClient::query()->update(['revoked_at' => now()]);
        $this->withToken($this->token)->getJson('/v1/status')->assertStatus(401);
    }

    public function test_tdlib_connection_failure_is_redacted(): void
    {
        Http::fake(fn () => throw new ConnectionException('contains-sensitive-upstream-detail'));
        $response = $this->withToken($this->token)->getJson('/v1/status');
        $response->assertStatus(503)->assertJsonPath('error.code', 'service.tdlib_unavailable');
        $this->assertStringNotContainsString('sensitive', $response->getContent());
    }

    public function test_tdlib_malformed_response_is_unavailable(): void
    {
        Http::fake(['*' => Http::response(['raw' => 'tdlib'])]);
        $this->withToken($this->token)->getJson('/v1/status')->assertStatus(503)->assertJsonPath('error.code', 'service.tdlib_unavailable');
    }

    public function test_request_state_does_not_leak_between_requests(): void
    {
        Http::fake(['*' => Http::response($this->readyTdlib())]);
        $first = $this->withToken($this->token)->getJson('/v1/status');
        $second = $this->withToken($this->token)->getJson('/v1/status');
        $this->assertNotSame($first->json('request_id'), $second->json('request_id'));
    }

    public function test_authenticated_status_is_rate_limited_per_client(): void
    {
        Http::fake(['*' => Http::response($this->readyTdlib())]);
        for ($attempt = 0; $attempt < 60; $attempt++) {
            $this->withToken($this->token)->getJson('/v1/status')->assertOk();
        }

        $this->withToken($this->token)->getJson('/v1/status')
            ->assertStatus(429)
            ->assertHeader('Retry-After')
            ->assertJsonPath('error.code', 'rate_limit.exceeded');
    }

    public function test_authentication_reports_database_unavailability(): void
    {
        $original = Config::get('database.connections.pgsql');
        DB::purge('pgsql');
        Config::set('database.connections.pgsql.port', 1);
        try {
            $this->withToken($this->token)->getJson('/v1/status')
                ->assertStatus(503)
                ->assertJsonPath('error.code', 'service.database_unavailable');
        } finally {
            Config::set('database.connections.pgsql', $original);
            DB::purge('pgsql');
        }
    }

    public function test_contradictory_tdlib_readiness_is_rejected(): void
    {
        $payload = $this->readyTdlib();
        $payload['data']['client_manager'] = 'not_ready';
        Http::fake(['*' => Http::response($payload)]);
        $this->withToken($this->token)->get('/v1/status')
            ->assertStatus(503)->assertJsonPath('error.code', 'service.tdlib_unavailable');
    }

    public function test_invalid_tdlib_contract_variants_are_redacted(): void
    {
        $variants = [];
        foreach (['service_version', 'client_manager', 'tdlib_version', 'accounts'] as $missing) {
            $variant = $this->readyTdlib();
            unset($variant['data'][$missing]);
            $variants[] = $variant;
        }
        foreach ([['tdlib_version', ''], ['accounts', -1], ['accounts', '0'], ['service_version', '01.0.0']] as [$key, $value]) {
            $variant = $this->readyTdlib();
            $variant['data'][$key] = $value;
            $variants[] = $variant;
        }
        foreach ($variants as $variant) {
            Http::fake(['*' => Http::response($variant)]);
            $this->withToken($this->token)->get('/v1/status')
                ->assertStatus(503)->assertJsonPath('error.code', 'service.tdlib_unavailable');
        }
        Http::fake(['*' => Http::response(['error' => ['code' => 'secret-upstream']], 401)]);
        $response = $this->withToken($this->token)->get('/v1/status');
        $response->assertStatus(503)->assertJsonPath('error.code', 'service.tdlib_unavailable');
        $this->assertStringNotContainsString('secret-upstream', $response->getContent());
    }

    #[DataProvider('rateLimiterStages')]
    public function test_rate_limiter_database_failures_keep_the_public_json_contract(string $stage): void
    {
        $failure = new QueryException('pgsql', 'select secret', [], new \RuntimeException('proxy-secret-sentinel'));
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
        $response->assertStatus(503)->assertJsonPath('error.code', 'service.database_unavailable')
            ->assertJsonStructure(['request_id']);
        $this->assertStringContainsString('application/json', (string) $response->headers->get('Content-Type'));
        $this->assertStringContainsString('no-store', (string) $response->headers->get('Cache-Control'));
        $this->assertStringNotContainsString('sentinel', $response->getContent());
    }

    /** @return array<string, array{string}> */
    public static function rateLimiterStages(): array
    {
        return [
            'read' => ['tooManyAttempts'],
            'increment' => ['hit'],
            'retry-after' => ['availableIn'],
        ];
    }

    /** @return array<string, mixed> */
    private function readyTdlib(): array
    {
        return json_decode((string) file_get_contents(base_path('../backend-tdlib/tests/contracts/tdlib_status_ready.json')), true, flags: JSON_THROW_ON_ERROR);
    }
}
