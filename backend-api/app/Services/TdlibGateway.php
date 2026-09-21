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

    /** @param array<string, mixed> $proxy
     * @return array<string, mixed>
     */
    public function pingProxy(string $accountId, array $proxy, string $requestId): array
    {
        return $this->request('POST', "/internal/v1/accounts/{$accountId}/proxy/ping", ['proxy' => $proxy], $requestId);
    }

    /** @param array<string, mixed> $command
     * @return array<string, mixed>
     */
    public function remove(string $accountId, array $command, string $requestId): array
    {
        return $this->request('DELETE', "/internal/v1/accounts/{$accountId}", $command, $requestId);
    }

    /** @param array<string, mixed> $query
     * @return array<string, mixed>
     */
    public function chats(string $accountId, array $query, string $requestId): array
    {
        return $this->request('GET', "/internal/v1/accounts/{$accountId}/chats", $query, $requestId, true);
    }

    /** @param array<string, mixed> $query
     * @return array<string, mixed>
     */
    public function chat(string $accountId, string $chatId, array $query, string $requestId): array
    {
        return $this->request('GET', "/internal/v1/accounts/{$accountId}/chats/{$chatId}", $query, $requestId, true);
    }

    /** @param array<string, mixed> $query
     * @return array<string, mixed>
     */
    public function messages(string $accountId, string $chatId, array $query, string $requestId): array
    {
        return $this->request('GET', "/internal/v1/accounts/{$accountId}/chats/{$chatId}/messages", $query, $requestId, true);
    }

    /** @param array<string, mixed> $query
     * @return array<string, mixed>
     */
    public function message(string $accountId, string $chatId, string $messageId, array $query, string $requestId): array
    {
        return $this->request('GET', "/internal/v1/accounts/{$accountId}/chats/{$chatId}/messages/{$messageId}", $query, $requestId, true);
    }

    /** @param array<string, mixed> $query
     * @return array<string, mixed>
     */
    public function updates(string $accountId, array $query, string $requestId): array
    {
        return $this->request('GET', "/internal/v1/accounts/{$accountId}/updates", $query, $requestId, true);
    }

    /** @param array<string, mixed> $lease
     * @return array<string, mixed>
     */
    public function interest(string $method, string $accountId, string $chatId, string $viewId, array $lease, string $requestId): array
    {
        return $this->request($method, "/internal/v1/accounts/{$accountId}/chats/{$chatId}/interests/{$viewId}", $lease, $requestId);
    }

    public function releasePrincipalInterests(string $type, string $id, string $requestId): void
    {
        $this->request('DELETE', '/internal/v1/interests/principal', ['principal_type' => $type,
            'principal_id' => $id], $requestId);
    }

    private function client(string $requestId, bool $read): PendingRequest
    {
        return Http::baseUrl((string) config('telebezel.tdlib.base_url'))
            ->withToken((string) config('telebezel.tdlib.token'))
            ->withHeaders(['X-Request-ID' => $requestId])
            ->acceptJson()
            ->asJson()
            ->connectTimeout(1)
            ->timeout(10);
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
            'telegram.operation_failed', 'proxy.unreachable',
            'chat.not_found', 'message.not_found', 'cursor.invalid', 'cursor.unusable', 'sync.resync_required',
            'read.deadline', 'interest.limit_reached',
        ];
        $code = $json['error']['code'] ?? null;
        if (! is_string($code) || ! in_array($code, $safeCodes, true)) {
            throw new ApiException('service.tdlib_unavailable', 503);
        }

        throw new ApiException($code, in_array($response->status(), [409, 422, 429, 503, 504], true) ? $response->status() : 502);
    }
}
