<?php

use App\Data\TokenClaims;
use App\Enums\AccountLifecycle;
use App\Enums\TokenType;
use App\Exceptions\ApiException;
use App\Models\AccessToken;
use App\Models\TelegramAccount;
use Illuminate\Support\Facades\Http;
use Illuminate\Support\Str;

function permissionsTestAccount(string $label): TelegramAccount
{
    return TelegramAccount::query()->create([
        'label' => $label,
        'storage_generation' => (string) Str::uuid(),
        'lifecycle' => AccountLifecycle::Active,
        'desired_revision' => 1,
        'applied_revision' => 1,
        'effective_config_id' => (string) Str::uuid(),
    ]);
}

test('each token type holds only its own permissions', function (): void {
    expect(TokenType::Maintenance->permissionNames())->not->toContain('messages.read')->not->toContain('messages.send');
    expect(TokenType::Device->permissionNames())->not->toContain('settings.manage')->not->toContain('accounts.manage')->not->toContain('quick_replies.manage');
    expect(fn () => TokenClaims::issue(TokenType::Maintenance, ['messages.read']))->toThrow(ApiException::class, 'request.invalid');
    expect(fn () => TokenClaims::issue(TokenType::Device, ['settings.manage']))->toThrow(ApiException::class, 'request.invalid');
    expect(fn () => TokenClaims::issue(TokenType::Device, ['messages.read'], ['not-a-uuid']))->toThrow(ApiException::class, 'request.invalid');
    expect(TokenClaims::issue(TokenType::Device, ['messages.read', 'messages.read'])->permissions)->toBe(['messages.read']);
});

test('a stored claim outside the token type is refused at authentication', function (): void {
    $forged = issueTestToken(TokenType::Maintenance, ['accounts.read', 'messages.read']);
    $this->withToken($forged['token'])->getJson('/v1/telegram/accounts')->assertUnauthorized()->assertJsonPath('error.code', 'auth.unauthorized');
    $unknown = issueTestToken(TokenType::Device, ['everything']);
    $this->withToken($unknown['token'])->getJson('/v1/status')->assertUnauthorized();
});

test('maintenance tokens cannot read conversations and device tokens cannot manage the server', function (): void {
    $account = permissionsTestAccount('Primary');
    $maintenance = issueTestToken(TokenType::Maintenance)['token'];
    $device = issueTestToken(TokenType::Device)['token'];
    $this->withToken($maintenance)->getJson("/v1/telegram/accounts/{$account->id}/chats")->assertForbidden()->assertJsonPath('error.code', 'auth.insufficient_scope');
    $this->withToken($maintenance)->getJson("/v1/telegram/accounts/{$account->id}/updates")->assertForbidden();
    $this->withToken($maintenance)->getJson('/v1/device/preferences')->assertForbidden();
    $this->withToken($device)->getJson('/v1/settings')->assertForbidden();
    $this->withToken($device)->getJson('/v1/devices')->assertForbidden();
    $this->withToken($device)->postJson('/v1/quick-replies', [
        'text' => 'nope',
    ])->assertForbidden();
    $this->withToken($device)->getJson('/v1/quick-replies')->assertOk()->assertJsonPath('data.items', [])->assertJsonPath('data.revision', 1);
    $this->withToken($maintenance)->getJson('/v1/quick-replies')->assertOk();
});

test('an account outside the claims is indistinguishable from a missing one', function (): void {
    $allowed = permissionsTestAccount('Allowed');
    $hidden = permissionsTestAccount('Hidden');
    $token = issueTestToken(TokenType::Device, accounts: [$allowed->id])['token'];
    Http::fake([
        '*' => Http::response([
            'data' => [
                'accounts' => [],
            ],
        ]),
    ]);
    $this->withToken($token)->getJson("/v1/telegram/accounts/{$hidden->id}/chats")->assertNotFound()->assertJsonPath('error.code', 'account.not_found');
    $this->withToken($token)->getJson("/v1/telegram/accounts/{$hidden->id}")->assertNotFound()->assertJsonPath('error.code', 'account.not_found');
    $this->withToken($token)->getJson('/v1/telegram/accounts/'.strtoupper($hidden->id))->assertNotFound();
    $this->withToken($token)->getJson('/v1/telegram/accounts/'.Str::uuid())->assertNotFound()->assertJsonPath('error.code', 'account.not_found');
    $ids = collect($this->withToken($token)->getJson('/v1/telegram/accounts')->assertOk()->json('data'))->pluck('id')->all();
    expect($ids)->toBe([$allowed->id]);
    Http::assertNotSent(fn ($request): bool => str_contains($request->url(), $hidden->id));
});

test('a revoked token stops working on the next request', function (): void {
    $device = issueTestToken(TokenType::Device);
    $this->withToken($device['token'])->getJson('/v1/device/preferences')->assertOk();
    AccessToken::query()->whereKey($device['id'])->update([
        'revoked_at' => now(),
    ]);
    $this->withToken($device['token'])->getJson('/v1/device/preferences')->assertUnauthorized();
});

test('quick reply changes advance the revision seen by devices', function (): void {
    $web = issueWebSession()['token'];
    $device = issueTestToken(TokenType::Device)['token'];
    $first = asWebSession($this, $web)->postJson('/v1/quick-replies', [
        'text' => 'On my way',
    ])->assertCreated()->json('data.id');
    $snapshot = $this->withToken($device)->getJson('/v1/quick-replies')->assertOk();
    $snapshot->assertJsonPath('data.items.0.text', 'On my way')->assertJsonPath('data.revision', 2);
    asWebSession($this, $web)->putJson('/v1/quick-replies/'.$first, [
        'text' => 'Running late',
    ])->assertOk();
    asWebSession($this, $web)->deleteJson('/v1/quick-replies/'.Str::uuid())->assertOk();
    $this->withToken($device)->getJson('/v1/quick-replies')->assertJsonPath('data.revision', 3)->assertJsonPath('data.items.0.text', 'Running late');
});
