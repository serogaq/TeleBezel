<?php

namespace App\Services;

use App\Exceptions\ApiException;
use Illuminate\Http\Client\ConnectionException;
use Illuminate\Http\Client\PendingRequest;
use Illuminate\Http\Client\Response;
use Illuminate\Support\Facades\Http;

final class TdlibGateway
{
    /** @param array<int, string> $accountIds
     * @return array<string, mixed>
     */
    public function listSnapshots(array $accountIds, string $requestId): array
    {
        return $this->request('GET', '/internal/v1/accounts', ['ids' => implode(',', $accountIds)], $requestId, true);
    }

    /** @return array<string, mixed> */
    public function snapshot(string $accountId, string $requestId): array
    {
        return $this->request('GET', "/internal/v1/accounts/{$accountId}", [], $requestId, true);
    }

    /** @param array<string, mixed> $command
     * @return array<string, mixed>
     */
    public function provision(string $accountId, array $command, string $requestId): array
    {
        return $this->request('PUT', "/internal/v1/accounts/{$accountId}", $command, $requestId);
    }

    /** @param array<string, mixed> $command
     * @return array<string, mixed>
     */
    public function authorizationAction(string $accountId, array $command, string $requestId): array
    {
        return $this->request('POST', "/internal/v1/accounts/{$accountId}/authorization/actions", $command, $requestId);
    }

    /** @param array<string, mixed> $command
     * @return array<string, mixed>
     */
    public function logout(string $accountId, array $command, string $requestId): array
    {
        return $this->request('POST', "/internal/v1/accounts/{$accountId}/logout", $command, $requestId);
    }

    /** @param array<string, mixed> $command
     * @return array<string, mixed>
     */
    public function updateProxy(string $accountId, array $command, string $requestId): array
    {
        return $this->request('PUT', "/internal/v1/accounts/{$accountId}/proxy", $command, $requestId);
    }

    /** @param array<string, mixed> $command
     * @return array<string, mixed>
     */
    public function remove(string $accountId, array $command, string $requestId): array
    {
        return $this->request('DELETE', "/internal/v1/accounts/{$accountId}", $command, $requestId);
    }

    private function client(string $requestId, bool $read): PendingRequest
    {
        return Http::baseUrl((string) config('telebezel.tdlib.base_url'))
            ->withToken((string) config('telebezel.tdlib.token'))
            ->withHeaders(['X-Request-ID' => $requestId])
            ->acceptJson()
            ->asJson()
            ->connectTimeout(1)
            ->timeout($read ? 2 : 10);
    }

    /** @param array<string, mixed> $payload
     * @return array<string, mixed>
     */
    private function request(string $method, string $path, array $payload, string $requestId, bool $read = false): array
    {
        try {
            $response = $this->client($requestId, $read)->send($method, $path, $method === 'GET' ? ['query' => $payload] : ['json' => $payload]);
        } catch (ConnectionException) {
            throw new ApiException('service.tdlib_unavailable', 503);
        }

        return $this->decode($response);
    }

    /** @return array<string, mixed> */
    private function decode(Response $response): array
    {
        $json = $response->json();
        if (! is_array($json)) {
            throw new ApiException('service.tdlib_unavailable', 503);
        }
        if ($response->successful()) {
            $data = $json['data'] ?? null;

            return is_array($data) ? $data : [];
        }

        $safeCodes = [
            'authorization.invalid_code', 'authorization.invalid_password', 'authorization.code_expired',
            'authorization.invalid_state', 'authorization.flood_wait', 'authorization.unsupported_state',
            'authorization.unsupported_delivery', 'operation.conflict', 'operation.outcome_unknown',
            'storage.missing', 'storage.identity_mismatch', 'storage.corrupt', 'storage.unsafe_path', 'storage.invalid_key',
            'storage.io_error', 'storage.volume_in_use',
            'configuration.missing', 'configuration.invalid', 'configuration.environment_mismatch', 'service.busy', 'service.stopping',
            'telegram.operation_failed',
        ];
        $code = $json['error']['code'] ?? null;
        if (! is_string($code) || ! in_array($code, $safeCodes, true)) {
            throw new ApiException('service.tdlib_unavailable', 503);
        }

        throw new ApiException($code, in_array($response->status(), [409, 422, 429, 503, 504], true) ? $response->status() : 502);
    }
}
