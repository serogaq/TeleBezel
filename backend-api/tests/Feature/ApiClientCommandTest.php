<?php

namespace Tests\Feature;

use App\Models\ApiClient;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\TestCase;

final class ApiClientCommandTest extends TestCase
{
    use RefreshDatabase;

    public function test_issue_and_revoke_commands_store_only_a_hash(): void
    {
        $this->artisan('telebezel:api-client-issue', ['name' => 'Pebble'])->assertSuccessful();
        $client = ApiClient::query()->sole();
        $this->assertSame(64, strlen($client->token_hash));
        $this->assertStringStartsWith('tb_', $client->token_prefix);
        $this->artisan('telebezel:api-client-revoke', ['id' => $client->getKey()])->assertSuccessful();
        $this->assertNotNull($client->fresh()->revoked_at);
    }
}
