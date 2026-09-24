<?php

use App\Contracts\Repositories\AccessTokenRepository;
use App\Contracts\Repositories\DeviceRepository;
use App\Contracts\TransactionManager;
use App\Data\PrincipalContext;
use App\Enums\TokenType;
use App\Exceptions\ApiException;
use App\Services\AccessTokenService;
use App\Services\DeviceService;
use Tests\Fakes\FakeTdlibGateway;

function deviceServiceFor(AccessTokenRepository $tokens, FakeTdlibGateway $tdlib, ?DeviceRepository $devices = null): DeviceService
{
    return new DeviceService($devices ?? Mockery::mock(DeviceRepository::class), $tokens, new AccessTokenService($tokens, $tdlib), app(TransactionManager::class));
}

test('device preferences reject maintenance scope before touching persistence', function () {
    $repository = Mockery::mock(DeviceRepository::class);
    $repository->shouldNotReceive('preferences');
    $service = deviceServiceFor(Mockery::mock(AccessTokenRepository::class), new FakeTdlibGateway, $repository);
    expect(fn () => $service->preferences(new PrincipalContext('maintenance', 'maintenance-id')))
        ->toThrow(ApiException::class, 'auth.insufficient_scope');
});

test('device revocation remains durable when runtime lease cleanup is unavailable', function () {
    $tokens = Mockery::mock(AccessTokenRepository::class);
    $revoked = false;
    $tokens->shouldReceive('revoke')->once()->with('device', 'instance', TokenType::Device)->andReturnUsing(function () use (&$revoked) {
        $revoked = true;

        return TokenType::Device;
    });
    $tdlib = new FakeTdlibGateway;
    $tdlib->queue('releasePrincipalInterests', function ($arguments) use (&$revoked) {
        expect($revoked)->toBeTrue();
        expect($arguments)->toBe([
            'type' => 'device',
            'id' => 'device',
            'requestId' => 'request',
        ]);
        throw new ApiException('service.tdlib_unavailable', 503);
    });
    deviceServiceFor($tokens, $tdlib)->revoke('instance', 'device', 'request');
    $tdlib->assertDrained();
});

test('an unknown device is not found and never releases runtime leases', function () {
    $tokens = Mockery::mock(AccessTokenRepository::class);
    $tokens->shouldReceive('revoke')->once()->andReturnNull();
    $tdlib = new FakeTdlibGateway;
    expect(fn () => deviceServiceFor($tokens, $tdlib)->revoke('instance', 'device', 'request'))
        ->toThrow(ApiException::class, 'device.not_found');
    expect($tdlib->calls)->toBe([]);
});
