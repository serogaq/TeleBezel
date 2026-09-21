<?php

declare(strict_types=1);

namespace App\Contracts;

interface TdlibGateway
{
    /** @param array<int, string> $accountIds
     * @return array<string, mixed>
     */
    public function listSnapshots(array $accountIds, string $requestId): array;

    /** @return array<string, mixed> */
    public function snapshot(string $accountId, string $requestId): array;

    /** @param array<string, mixed> $command
     * @return array<string, mixed>
     */
    public function provision(string $accountId, array $command, string $requestId): array;

    /** @param array<string, mixed> $command
     * @return array<string, mixed>
     */
    public function authorizationAction(string $accountId, array $command, string $requestId): array;

    /** @param array<string, mixed> $command
     * @return array<string, mixed>
     */
    public function logout(string $accountId, array $command, string $requestId): array;

    /** @param array<string, mixed> $command
     * @return array<string, mixed>
     */
    public function updateProxy(string $accountId, array $command, string $requestId): array;

    /** @param array<string, mixed> $proxy
     * @return array<string, mixed>
     */
    public function pingProxy(string $accountId, array $proxy, string $requestId): array;

    /** @param array<string, mixed> $command
     * @return array<string, mixed>
     */
    public function remove(string $accountId, array $command, string $requestId): array;

    /** @param array<string, mixed> $query
     * @return array<string, mixed>
     */
    public function chats(string $accountId, array $query, string $requestId): array;

    /** @param array<string, mixed> $query
     * @return array<string, mixed>
     */
    public function chat(string $accountId, string $chatId, array $query, string $requestId): array;

    /** @param array<string, mixed> $query
     * @return array<string, mixed>
     */
    public function messages(string $accountId, string $chatId, array $query, string $requestId): array;

    /** @param array<string, mixed> $query
     * @return array<string, mixed>
     */
    public function message(string $accountId, string $chatId, string $messageId, array $query, string $requestId): array;

    /** @param array<string, mixed> $query
     * @return array<string, mixed>
     */
    public function updates(string $accountId, array $query, string $requestId): array;

    /** @param array<string, mixed> $lease
     * @return array<string, mixed>
     */
    public function interest(string $method, string $accountId, string $chatId, string $viewId, array $lease, string $requestId): array;

    public function releasePrincipalInterests(string $type, string $id, string $requestId): void;
}
