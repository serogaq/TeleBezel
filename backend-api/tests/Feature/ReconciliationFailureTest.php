<?php

use App\Cache\ReconciliationCacheKeys;
use App\Contracts\MonotonicClock;
use App\Contracts\TdlibGateway;
use App\Enums\AccountLifecycle;
use App\Exceptions\ApiException;
use App\Models\TelegramAccount;
use App\Services\ReconciliationService;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Cache;
use Tests\Fakes\FakeMonotonicClock;
use Tests\Fakes\FakeTdlibGateway;

uses(RefreshDatabase::class);

beforeEach(function () {
    config([
        'cache.default' => 'array',
    ]);
    $this->gateway = new FakeTdlibGateway;
    $this->app->instance(TdlibGateway::class, $this->gateway);
    $this->account = TelegramAccount::query()->create([
        'label' => 'Failure test',
        'storage_generation' => fake()->uuid(),
        'lifecycle' => AccountLifecycle::Provisioning,
        'desired_revision' => 1,
    ]);
});

test('transient provision failure is retried after backoff and never reported as successful', function () {
    $this->gateway->queue('provision', new ApiException('service.tdlib_unavailable', 503, 120));
    $service = app(ReconciliationService::class);
    $service->run(false);
    $account = $this->account->fresh();
    expect($account->reconcile_failures)->toBe(1)
        ->and($account->last_reconcile_success_at)->toBeNull()
        ->and($account->next_reconcile_at->greaterThanOrEqualTo(now()->addSeconds(119)))->toBeTrue();
    $this->assertDatabaseHas('scheduler_statuses', [
        'name' => 'reconciliation',
        'succeeded' => 0,
        'failed' => 1,
    ]);
    $service->run(false);
    expect($this->gateway->calls)->toHaveCount(1);
    $this->travel(121)->seconds();
    $this->gateway->queue('provision', [
        'applied_revision' => 1,
        'runtime_available' => true,
    ])->queue('listSnapshots', [
        'accounts' => [],
    ])->queue('provision', [
        'applied_revision' => 1,
        'runtime_available' => true,
    ]);
    $service->run(false);
    expect($this->account->fresh()->reconcile_failures)->toBe(0)
        ->and($this->account->fresh()->lifecycle)->toBe(AccountLifecycle::Active);
    $this->gateway->assertDrained();
});

test('permanent failure blocks only the failing revision', function () {
    $this->gateway->queue('provision', new ApiException('configuration.invalid', 422));
    $service = app(ReconciliationService::class);
    $service->run(false);
    expect($this->account->fresh()->reconcile_blocked_revision)->toBe(1);
    $service->run(false);
    expect($this->gateway->calls)->toHaveCount(1);
    $this->account->update([
        'desired_revision' => 2,
    ]);
    $this->gateway->queue('provision', [
        'applied_revision' => 2,
        'runtime_available' => true,
    ])->queue('listSnapshots', [
        'accounts' => [],
    ])->queue('provision', [
        'applied_revision' => 2,
        'runtime_available' => true,
    ]);
    $service->run(false);
    expect($this->account->fresh()->reconcile_blocked_revision)->toBeNull();
    $this->gateway->assertDrained();
});

test('reconciliation respects the shared lock without dispatching work', function () {
    $lock = Cache::lock(ReconciliationCacheKeys::lock(), 120);
    expect($lock->get())->toBeTrue();
    try {
        expect(app(ReconciliationService::class)->run(false))->toBe(['Another reconciliation pass is active.']);
        expect($this->gateway->calls)->toBe([]);
        $this->assertDatabaseCount('scheduler_statuses', 0);
    } finally {
        $lock->release();
    }
});

test('an unexpected failure releases the lock for the next reconciliation pass', function () {
    $this->gateway->queue('provision', new LogicException('injected failure'));
    expect(fn () => app(ReconciliationService::class)->run(false))->toThrow(LogicException::class);
    $lock = Cache::lock(ReconciliationCacheKeys::lock(), 120);
    try {
        expect($lock->get())->toBeTrue();
    } finally {
        $lock->release();
    }
    $this->gateway->assertDrained();
});

test('time budget stops before the next account and resumes from the last attempted cursor', function () {
    $clock = new FakeMonotonicClock;
    $this->app->instance(MonotonicClock::class, $clock);
    TelegramAccount::query()->create([
        'label' => 'Second account',
        'storage_generation' => fake()->uuid(),
        'lifecycle' => AccountLifecycle::Provisioning,
        'desired_revision' => 1,
    ]);
    $ids = TelegramAccount::query()->orderBy('id')->pluck('id')->all();
    $this->gateway->queue('provision', function () use ($clock) {
        $clock->advanceSeconds(35);
        throw new ApiException('service.tdlib_unavailable', 503);
    });
    $service = app(ReconciliationService::class);
    $service->run(false);
    expect($this->gateway->calls)->toHaveCount(1)
        ->and($this->gateway->calls[0]['arguments']['accountId'])->toBe($ids[0])
        ->and(Cache::get(ReconciliationCacheKeys::cursor(0)))->toBe($ids[0]);
    $this->assertDatabaseHas('scheduler_statuses', [
        'name' => 'reconciliation',
        'last_result' => 'budget_exhausted',
        'failed' => 1,
    ]);
    $this->gateway->queue('provision', new ApiException('service.tdlib_unavailable', 503));
    $service->run(false);
    expect($this->gateway->calls)->toHaveCount(2)
        ->and($this->gateway->calls[1]['arguments']['accountId'])->toBe($ids[1]);
    $this->gateway->assertDrained();
});
