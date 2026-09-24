<?php

use App\Models\AccessToken;
use Illuminate\Support\Facades\Http;

test('issue and revoke commands store only a hash', function (): void {
    $this->artisan('telebezel:api-client-issue', [
        'name' => 'Pebble',
    ])->assertSuccessful();
    $client = AccessToken::query()->sole();
    expect(strlen($client->getAttribute('token_hash')))->toBe(64);
    expect($client->getAttribute('token_prefix'))->toStartWith('tb_');
    expect($client->type->value)->toBe('maintenance');
    expect($client->claims['permissions'])->toContain('settings.manage')->not->toContain('messages.read');
    Http::fake([
        '*' => Http::response([
            'data' => [
                'released' => 0,
            ],
        ]),
    ]);
    $this->artisan('telebezel:api-client-revoke', [
        'id' => $client->getKey(),
    ])->assertSuccessful();
    expect($client->fresh()->revoked_at)->not->toBeNull();
});

test('a device token can be issued from the command line', function (): void {
    $this->artisan('telebezel:api-client-issue', [
        'name' => 'Watch',
        '--type' => 'device',
    ])->assertSuccessful();
    $token = AccessToken::query()->sole();
    expect($token->type->value)->toBe('device');
    expect($token->claims['permissions'])->toContain('messages.send')->not->toContain('accounts.manage');
    $this->artisan('telebezel:api-client-issue', [
        'name' => 'Bad',
        '--type' => 'owner',
    ])->assertFailed();
});
