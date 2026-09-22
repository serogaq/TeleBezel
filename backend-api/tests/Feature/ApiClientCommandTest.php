<?php

use App\Models\ApiClient;

test('issue and revoke commands store only a hash', function (): void {
    $this->artisan('telebezel:api-client-issue', [
        'name' => 'Pebble',
    ])->assertSuccessful();
    $client = ApiClient::query()->sole();
    expect(strlen($client->token_hash))->toBe(64);
    expect($client->token_prefix)->toStartWith('tb_');
    $this->artisan('telebezel:api-client-revoke', [
        'id' => $client->getKey(),
    ])->assertSuccessful();
    expect($client->fresh()->revoked_at)->not->toBeNull();
});
