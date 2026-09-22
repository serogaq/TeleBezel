<?php

use App\Data\AccountData;
use App\Enums\AccountLifecycle;
use App\Exceptions\ApiException;
use App\Services\AccountStatePolicy;

function policyAccount(AccountLifecycle $lifecycle = AccountLifecycle::Active): AccountData
{
    return new AccountData('00112233-4455-4677-8899-aabbccddeeff', 'Private label', '11112233-4455-4677-8899-aabbccddeeff', $lifecycle,
        3, 2, null, null, null, null, null, 0, true, 'ready', 'ready', null, 'operation', null, 1, 'config', null, null, null, 0, null);
}

test('logout increments authorization generation only for a new intent', function () {
    $policy = new AccountStatePolicy;
    $changes = $policy->logout(policyAccount());
    expect($changes['desired_revision'])->toBe(4)
        ->and($changes['authorization_generation'])->toBe(2)
        ->and($changes['operation_id'])->toBe($changes['logout_operation_id'])
        ->and($policy->logout(policyAccount(AccountLifecycle::LogoutPending)))->toBe([]);
});

test('removal cannot be reversed by logout or proxy replacement', function () {
    $policy = new AccountStatePolicy;
    $account = policyAccount(AccountLifecycle::Removing);
    expect(fn () => $policy->logout($account))->toThrow(ApiException::class, 'operation.conflict');
    expect(fn () => $policy->updateProxy($account, 3, [
        'mode' => 'inherit',
    ]))->toThrow(ApiException::class, 'operation.conflict');
    expect($policy->remove($account))->toBe([]);
});

test('acknowledgement from stale revision generation or configuration cannot mutate state', function (string $field, mixed $value) {
    $expected = policyAccount();
    $current = clone $expected;
    $current->{$field} = $value;
    expect((new AccountStatePolicy)->confirm($current, $expected, [
        'applied_revision' => 3,
        'completed' => true,
    ]))->toBe([]);
})->with([
    'revision' => ['desired_revision', 4],
    'storage generation' => ['storage_generation', 'another-generation'],
    'configuration' => ['effective_config_id', 'another-config'],
    'tombstone' => ['lifecycle', AccountLifecycle::Removed],
]);

test('removal acknowledgement scrubs data even if upstream advertises a live runtime', function () {
    $account = policyAccount(AccountLifecycle::Removing);
    $changes = (new AccountStatePolicy)->confirm($account, $account, [
        'applied_revision' => 3,
        'completed' => true,
        'runtime_available' => true,
        'authorization_state' => 'ready',
    ]);
    expect($changes['lifecycle'])->toBe(AccountLifecycle::Removed)
        ->and($changes['runtime_available'])->toBeFalse()
        ->and($changes['label'])->toBeNull()
        ->and($changes['authorization_state'])->toBeNull()
        ->and($changes['operation_id'])->toBeNull();
});
