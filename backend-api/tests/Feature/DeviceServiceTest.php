<?php

use App\Contracts\Repositories\DeviceRepository;
use App\Data\PrincipalContext;
use App\Exceptions\ApiException;
use App\Services\DeviceService;
use Tests\Fakes\FakeTdlibGateway;

test('device preferences reject owner scope before touching persistence', function () {
    $repository = Mockery::mock(DeviceRepository::class);
    $repository->shouldNotReceive('preferences');
    $service = new DeviceService($repository, new FakeTdlibGateway);
    expect(fn () => $service->preferences(new PrincipalContext('owner', 'owner-id')))
        ->toThrow(ApiException::class, 'auth.insufficient_scope');
});

test('device revocation remains durable when runtime lease cleanup is unavailable', function () {
    $repository = Mockery::mock(DeviceRepository::class);
    $revoked = false;
    $repository->shouldReceive('revoke')->once()->with('instance', 'device')->andReturnUsing(function () use (&$revoked) {
        $revoked = true;
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
    (new DeviceService($repository, $tdlib))->revoke('instance', 'device', 'request');
    $tdlib->assertDrained();
});

test('failed revocation never releases runtime leases', function () {
    $repository = Mockery::mock(DeviceRepository::class);
    $repository->shouldReceive('revoke')->once()->andThrow(new ApiException('device.not_found', 404));
    $tdlib = new FakeTdlibGateway;
    expect(fn () => (new DeviceService($repository, $tdlib))->revoke('instance', 'device', 'request'))
        ->toThrow(ApiException::class, 'device.not_found');
    expect($tdlib->calls)->toBe([]);
});
