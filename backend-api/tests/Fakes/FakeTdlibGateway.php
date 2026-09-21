<?php

declare(strict_types=1);

namespace Tests\Fakes;

use App\Contracts\TdlibGateway;
use Closure;
use LogicException;
use Throwable;

final class FakeTdlibGateway implements TdlibGateway
{
    /** @var array<string, list<array<string, mixed>|Throwable|Closure(array<string, mixed>): array<string, mixed>>> */
    private array $responses = [];

    /** @var list<array{method: string, arguments: array<string, mixed>}> */
    public array $calls = [];

    /** @param array<string, mixed>|Throwable|Closure(array<string, mixed>): array<string, mixed> $response */
    public function queue(string $method, array|Throwable|Closure $response): self
    {
        if (! method_exists(TdlibGateway::class, $method)) {
            throw new LogicException('Unknown TDLib operation: '.$method);
        }
        $this->responses[$method][] = $response;

        return $this;
    }

    /** @param array<string, mixed> $arguments
     * @return array<string, mixed>
     */
    private function dispatch(string $method, array $arguments): array
    {
        $this->calls[] = [
            'method' => $method,
            'arguments' => $arguments,
        ];
        if (($this->responses[$method] ?? []) === []) {
            throw new LogicException('Unexpected TDLib call: '.$method);
        }
        $response = array_shift($this->responses[$method]);

        if ($response instanceof Throwable) {
            throw $response;
        }

        return $response instanceof Closure ? $response($arguments) : $response;
    }

    public function assertDrained(): void
    {
        foreach ($this->responses as $method => $responses) {
            if ($responses !== []) {
                throw new LogicException('Unused TDLib responses: '.$method);
            }
        }
    }

    /** @param array<int, string> $accountIds
     * @return array<string, mixed>
     */
    public function listSnapshots(array $accountIds, string $requestId): array
    {
        return $this->dispatch('listSnapshots', get_defined_vars());
    }

    /** @return array<string, mixed> */
    public function snapshot(string $accountId, string $requestId): array
    {
        return $this->dispatch('snapshot', get_defined_vars());
    }

    /** @param array<string, mixed> $command
     * @return array<string, mixed>
     */
    public function provision(string $accountId, array $command, string $requestId): array
    {
        return $this->dispatch('provision', get_defined_vars());
    }

    /** @param array<string, mixed> $command
     * @return array<string, mixed>
     */
    public function authorizationAction(string $accountId, array $command, string $requestId): array
    {
        return $this->dispatch('authorizationAction', get_defined_vars());
    }

    /** @param array<string, mixed> $command
     * @return array<string, mixed>
     */
    public function logout(string $accountId, array $command, string $requestId): array
    {
        return $this->dispatch('logout', get_defined_vars());
    }

    /** @param array<string, mixed> $command
     * @return array<string, mixed>
     */
    public function updateProxy(string $accountId, array $command, string $requestId): array
    {
        return $this->dispatch('updateProxy', get_defined_vars());
    }

    /** @param array<string, mixed> $proxy
     * @return array<string, mixed>
     */
    public function pingProxy(string $accountId, array $proxy, string $requestId): array
    {
        return $this->dispatch('pingProxy', get_defined_vars());
    }

    /** @param array<string, mixed> $command
     * @return array<string, mixed>
     */
    public function remove(string $accountId, array $command, string $requestId): array
    {
        return $this->dispatch('remove', get_defined_vars());
    }

    /** @param array<string, mixed> $query
     * @return array<string, mixed>
     */
    public function chats(string $accountId, array $query, string $requestId): array
    {
        return $this->dispatch('chats', get_defined_vars());
    }

    /** @param array<string, mixed> $query
     * @return array<string, mixed>
     */
    public function chat(string $accountId, string $chatId, array $query, string $requestId): array
    {
        return $this->dispatch('chat', get_defined_vars());
    }

    /** @param array<string, mixed> $query
     * @return array<string, mixed>
     */
    public function messages(string $accountId, string $chatId, array $query, string $requestId): array
    {
        return $this->dispatch('messages', get_defined_vars());
    }

    /** @param array<string, mixed> $query
     * @return array<string, mixed>
     */
    public function message(string $accountId, string $chatId, string $messageId, array $query, string $requestId): array
    {
        return $this->dispatch('message', get_defined_vars());
    }

    /** @param array<string, mixed> $query
     * @return array<string, mixed>
     */
    public function updates(string $accountId, array $query, string $requestId): array
    {
        return $this->dispatch('updates', get_defined_vars());
    }

    /** @param array<string, mixed> $lease
     * @return array<string, mixed>
     */
    public function interest(string $method, string $accountId, string $chatId, string $viewId, array $lease, string $requestId): array
    {
        return $this->dispatch('interest', get_defined_vars());
    }

    public function releasePrincipalInterests(string $type, string $id, string $requestId): void
    {
        $this->dispatch('releasePrincipalInterests', get_defined_vars());
    }
}
