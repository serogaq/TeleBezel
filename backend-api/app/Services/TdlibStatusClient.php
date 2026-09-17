<?php

namespace App\Services;

use Illuminate\Http\Client\ConnectionException;
use Illuminate\Support\Facades\Http;
use RuntimeException;

final class TdlibStatusClient
{
    /** @return array{status: string, tdlib_version: string, accounts: int} */
    public function status(): array
    {
        try {
            $response = Http::baseUrl((string) config('telebezel.tdlib.base_url'))
                ->withToken((string) config('telebezel.tdlib.token'))
                ->acceptJson()
                ->connectTimeout(1)
                ->timeout(2)
                ->get('/internal/v1/status');
        } catch (ConnectionException $exception) {
            throw new RuntimeException('TDLib service connection failed.', previous: $exception);
        }

        if (! $response->successful()) {
            throw new RuntimeException('TDLib service returned an unsuccessful status.');
        }

        $data = $response->json('data');
        if (! is_array($data)
            || ($data['status'] ?? null) !== 'ready'
            || ($data['client_manager'] ?? null) !== 'ready'
            || ! is_string($data['service_version'] ?? null)
            || preg_match('/^(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)$/', $data['service_version']) !== 1
            || ! is_string($data['tdlib_version'] ?? null)
            || preg_match('/^[A-Za-z0-9][A-Za-z0-9.+_-]{0,63}$/', $data['tdlib_version']) !== 1
            || ! is_int($data['accounts'] ?? null)
            || $data['accounts'] < 0) {
            throw new RuntimeException('TDLib service returned an invalid response.');
        }

        return ['status' => 'ready', 'tdlib_version' => $data['tdlib_version'], 'accounts' => $data['accounts']];
    }
}
