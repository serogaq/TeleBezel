<?php

use App\Contracts\Repositories\TelegramAccountRepository;
use App\Data\TelegramId;
use App\Enums\AccountLifecycle;
use App\Exceptions\ApiException;
use App\Http\Resources\TelegramReadResource;
use App\Models\TelegramAccount;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Str;

uses(RefreshDatabase::class);
beforeEach(function () {
    asWebSession($this, issueWebSession()['token']);
});
test('quick reply reorder reaches its own route and creation survives a position gap', function () {
    $first = $this->postJson('/v1/quick-replies', [
        'text' => 'first',
    ])->assertCreated()->json('data.id');
    $second = $this->postJson('/v1/quick-replies', [
        'text' => 'second',
    ])->assertCreated()->json('data.id');
    $this->deleteJson('/v1/quick-replies/'.$first)->assertOk();
    $third = $this->postJson('/v1/quick-replies', [
        'text' => 'third',
    ])->assertCreated()->json('data.id');
    $this->putJson('/v1/quick-replies/reorder', [
        'ids' => [$third, $second],
    ])->assertOk()->assertJsonPath('data.0.id', $third);
    $this->putJson('/v1/quick-replies/reorder', [
        'ids' => [$third],
    ])->assertStatus(409);
});
test('a late snapshot cannot overwrite newer application intent', function () {
    $model = TelegramAccount::query()->create([
        'label' => 'test',
        'storage_generation' => (string) Str::uuid(),
        'lifecycle' => AccountLifecycle::Active,
        'desired_revision' => 1,
        'effective_config_id' => (string) Str::uuid(),
    ]);
    $repository = app(TelegramAccountRepository::class);
    $snapshot = $repository->find($model->id);
    $operation = (string) Str::uuid();
    $model->update([
        'desired_revision' => 2,
        'operation_id' => $operation,
    ]);
    $snapshot->operation_id = null;
    $repository->persistSnapshotState($snapshot);
    expect($model->fresh()->operation_id)->toBe($operation);
});
test('read resources preserve envelopes and discard undeclared nested upstream fields', function () {
    $resource = new TelegramReadResource([
        'item' => [
            'id' => '42',
            'title' => 'chat',
            'token' => 'secret',
            'last_message' => [
                'id' => '4',
                'content' => [
                    'kind' => 'text',
                    'text' => 'hello',
                    'password' => 'secret',
                ],
            ],
        ],
        'stale' => true,
        'partial' => false,
        'updates_cursor' => 'cursor',
        'source' => 'tdlib_memory',
        'credentials' => 'secret',
    ], 'chat');
    $body = $resource->respond()->getContent();
    expect($body)->toContain('"item"')->toContain('hello')->not->toContain('secret');
});
test('telegram ids reject values outside signed 64 bit range', function (string $id) {
    expect(fn () => new TelegramId($id))->toThrow(ApiException::class);
})->with(['9223372036854775808', '-9223372036854775809', '0', '01', '1e5']);
