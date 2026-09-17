<?php

namespace Tests\Feature;

use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Config;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Http;
use Tests\TestCase;

final class HealthTest extends TestCase
{
    use RefreshDatabase;

    public function test_liveness_does_not_require_dependencies(): void
    {
        $this->getJson('/healthz')->assertOk()->assertHeader('X-Request-ID')
            ->assertExactJson(['status' => 'ok', 'service' => 'backend-api', 'version' => '0.1.0']);
    }

    public function test_readiness_checks_postgres_and_tdlib(): void
    {
        Http::fake(['*' => Http::response(['data' => ['status' => 'ready', 'client_manager' => 'ready', 'service_version' => '0.1.0', 'tdlib_version' => '1.8.67', 'accounts' => 0]])]);
        $this->getJson('/readyz')->assertOk()->assertExactJson(['status' => 'ready']);
        Http::assertSent(fn ($request): bool => $request->hasHeader('Authorization', 'Bearer test-internal-token'));
    }

    public function test_readiness_is_unavailable_when_postgres_is_down(): void
    {
        $original = Config::get('database.connections.pgsql');
        DB::purge('pgsql');
        Config::set('database.connections.pgsql.port', 1);
        try {
            $this->getJson('/readyz')->assertStatus(503)->assertExactJson(['status' => 'not_ready']);
        } finally {
            Config::set('database.connections.pgsql', $original);
            DB::purge('pgsql');
        }
    }
}
