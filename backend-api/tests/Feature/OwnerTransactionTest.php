<?php

use App\Enums\TokenType;
use App\Exceptions\ApiException;
use App\Models\AccessToken;
use App\Models\Instance;
use App\Services\AccessTokenService;
use App\Services\AdministrationService;
use App\Services\AuthenticationService;
use App\Services\OwnerAccessService;
use Illuminate\Database\Events\QueryExecuted;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Hash;

uses(RefreshDatabase::class);

test('recovery is single use and revokes previous sessions and devices atomically', function () {
    $access = app(OwnerAccessService::class);
    [$id, $oldToken, $recovery] = $access->bootstrap(app(AdministrationService::class)->bootstrap(), 'old-password');
    $device = app(AccessTokenService::class)->issue($id, TokenType::Device, 'watch');
    [$newToken, $replacement] = $access->recover($recovery, 'new-password');
    expect($replacement)->not->toBe($recovery);
    expect(fn () => app(AuthenticationService::class)->token($oldToken))->toThrow(ApiException::class);
    expect(app(AuthenticationService::class)->token($newToken)->instanceId)->toBe($id);
    expect(AccessToken::query()->findOrFail($device['id'])->revoked_at)->not->toBeNull();
    expect(fn () => $access->recover($recovery, 'attacker-password'))->toThrow(ApiException::class, 'owner.invalid_recovery_code');
    expect(fn () => $access->login('old-password'))->toThrow(ApiException::class);
    expect($access->login('new-password')[0])->toBe($id);
});

test('session insertion failure rolls back recovery credentials and revocations', function () {
    $access = app(OwnerAccessService::class);
    [$id, $token, $recovery] = $access->bootstrap(app(AdministrationService::class)->bootstrap(), 'old-password');
    $device = app(AccessTokenService::class)->issue($id, TokenType::Device, 'watch');
    DB::listen(function (QueryExecuted $query) {
        if (str_starts_with($query->sql, 'insert into "access_tokens"')) {
            throw new RuntimeException('injected session storage failure');
        }
    });
    expect(fn () => $access->recover($recovery, 'new-password'))->toThrow(RuntimeException::class, 'injected session storage failure');
    $instance = Instance::query()->findOrFail($id);
    expect(Hash::check('old-password', $instance->owner_password_hash))->toBeTrue()
        ->and(Hash::check($recovery, $instance->recovery_code_hash))->toBeTrue()
        ->and(AccessToken::query()->findOrFail($device['id'])->revoked_at)->toBeNull();
    expect(app(AuthenticationService::class)->token($token)->instanceId)->toBe($id);
});

test('a second valid bootstrap code cannot create another owner', function () {
    $admin = app(AdministrationService::class);
    $first = $admin->bootstrap();
    $second = $admin->bootstrap();
    $access = app(OwnerAccessService::class);
    $access->bootstrap($first, 'password');
    expect(fn () => $access->bootstrap($second, 'other-password'))->toThrow(ApiException::class, 'bootstrap.consumed');
    $this->assertDatabaseCount('instances', 1);
    $this->assertDatabaseCount('access_tokens', 1);
});
