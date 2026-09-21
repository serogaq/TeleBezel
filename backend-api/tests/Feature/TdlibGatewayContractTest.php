<?php

use App\Contracts\TdlibGateway;
use App\Exceptions\ApiException;
use Illuminate\Support\Facades\Http;

// These are public contract guarantees, independent of any Telegram account.
test('gateway preserves documented errors and retry headers while discarding upstream details', function (string $code, int $status): void {
    Http::fake([
        '*' => Http::response([
            'error' => [
                'code' => $code,
                'message' => 'private telegram detail',
            ],
        ], $status, [
            'Retry-After' => '17',
        ]),
    ]);
    try {
        app(TdlibGateway::class)->snapshot('00112233-4455-4677-8899-aabbccddeeff', '10112233-4455-4677-8899-aabbccddeeff');
        test()->fail('Expected the gateway to reject an upstream error');
    } catch (ApiException $exception) {
        expect($exception->errorCode)->toBe($code)
            ->and($exception->status)->toBe($status)
            ->and($exception->retryAfter)->toBe(17)
            ->and($exception->getMessage())->not->toContain('private telegram detail');
    }
})->with([
    ['account.not_found', 404], ['account.gone', 410], ['operation.conflict', 409], ['operation.outcome_unknown', 504],
    ['authorization.invalid_code', 422], ['authorization.invalid_password', 422], ['authorization.code_expired', 422],
    ['authorization.invalid_state', 409], ['authorization.flood_wait', 429], ['authorization.unsupported_state', 422], ['authorization.unsupported_delivery', 422],
    ['storage.missing', 409], ['storage.identity_mismatch', 409], ['storage.corrupt', 409], ['storage.unsafe_path', 409], ['storage.invalid_key', 409], ['storage.io_error', 503], ['storage.volume_in_use', 503],
    ['configuration.missing', 409], ['configuration.invalid', 409], ['configuration.environment_mismatch', 409],
    ['service.busy', 503], ['service.stopping', 503], ['telegram.operation_failed', 502], ['proxy.unreachable', 502],
    ['chat.not_found', 404], ['message.not_found', 404], ['cursor.invalid', 409], ['cursor.unusable', 409], ['sync.resync_required', 409], ['read.deadline', 504], ['interest.limit_reached', 429],
]);

test('malformed and undocumented upstream responses become a safe unavailable error', function (mixed $body, int $status): void {
    Http::fake([
        '*' => Http::response($body, $status),
    ]);
    try {
        app(TdlibGateway::class)->chats('00112233-4455-4677-8899-aabbccddeeff', [], '10112233-4455-4677-8899-aabbccddeeff');
        test()->fail('Expected invalid response rejection');
    } catch (ApiException $exception) {
        expect($exception->errorCode)->toBe('service.tdlib_unavailable')->and($exception->status)->toBe(503);
    }
})->with([
    ['<html>SECRET</html>', 502], ['not json', 200], [[], 200], [[
        'data' => null,
    ], 200], [[
        'data' => 'SECRET',
    ], 200],
    [[
        'data' => [1, 2],
    ], 200], [[
        'error' => [
            'code' => 'SECRET',
        ],
    ], 500], [[
        'error' => 'SECRET',
    ], 500], [[], 401],
]);
