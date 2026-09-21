<?php

use App\Exceptions\ApiException;
use App\Models\Device;
use App\Models\Instance;
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
    Device::query()->create([
        'instance_id' => $id,
        'name' => 'watch',
        'token_prefix' => 'test',
        'token_hash' => hash('sha256', 'device'),
    ]);
    [$newToken, $replacement] = $access->recover($recovery, 'new-password');
    expect($replacement)->not->toBe($recovery);
    expect(fn () => app(AuthenticationService::class)->owner($oldToken))->toThrow(ApiException::class);
    expect(app(AuthenticationService::class)->owner($newToken)->instanceId)->toBe($id);
    expect(Device::query()->firstOrFail()->revoked_at)->not->toBeNull();
    expect(fn () => $access->recover($recovery, 'attacker-password'))->toThrow(ApiException::class, 'owner.invalid_recovery_code');
    expect(fn () => $access->login('old-password'))->toThrow(ApiException::class);
    expect($access->login('new-password')[0])->toBe($id);
});

test('session insertion failure rolls back recovery credentials and revocations', function () {
    $access = app(OwnerAccessService::class);
    [$id, $token, $recovery] = $access->bootstrap(app(AdministrationService::class)->bootstrap(), 'old-password');
    Device::query()->create([
        'instance_id' => $id,
        'name' => 'watch',
        'token_prefix' => 'test',
        'token_hash' => hash('sha256', 'device'),
    ]);
    DB::listen(function (QueryExecuted $query) {
        if (str_starts_with($query->sql, 'insert into "owner_sessions"')) {
            throw new RuntimeException('injected session storage failure');
        }
    });
    expect(fn () => $access->recover($recovery, 'new-password'))->toThrow(RuntimeException::class, 'injected session storage failure');
    $instance = Instance::query()->findOrFail($id);
    expect(Hash::check('old-password', $instance->owner_password_hash))->toBeTrue()
        ->and(Hash::check($recovery, $instance->recovery_code_hash))->toBeTrue()
        ->and(Device::query()->firstOrFail()->revoked_at)->toBeNull();
    expect(app(AuthenticationService::class)->owner($token)->instanceId)->toBe($id);
});

test('a second valid bootstrap code cannot create another owner', function () {
    $admin = app(AdministrationService::class);
    $first = $admin->bootstrap();
    $second = $admin->bootstrap();
    $access = app(OwnerAccessService::class);
    $access->bootstrap($first, 'password');
    expect(fn () => $access->bootstrap($second, 'other-password'))->toThrow(ApiException::class, 'bootstrap.consumed');
    $this->assertDatabaseCount('instances', 1);
    $this->assertDatabaseCount('owner_sessions', 1);
});
