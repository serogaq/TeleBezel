<?php

declare(strict_types=1);

namespace App\Contracts\Repositories;

use App\Data\AccountData;
use Closure;
use Illuminate\Pagination\LengthAwarePaginator;

interface TelegramAccountRepository
{
    /** @param array<string, mixed> $input
     * @param array<string, mixed> $proxy
     * @return array{AccountData, bool} */
    public function create(bool $owner, string $scopeId, string $keyHash, string $requestHash, array $input, array $proxy, int $attempt = 0): array;

    public function find(string $id): AccountData;

    /** @return LengthAwarePaginator<int, AccountData> */
    public function paginate(int $perPage, int $page): LengthAwarePaginator;

    public function isRemoved(string $id): bool;

    /** @param Closure(AccountData): array<string, mixed> $transition */
    public function mutate(string $id, Closure $transition): AccountData;

    public function persistSnapshotState(AccountData $account): void;

    public function recordDeferredError(AccountData $account, string $code): void;

    /** @return array<string, mixed> */
    public function configuration(): array;

    /** @param list<string> $lifecycles
     * @return array<int, AccountData> */
    public function batch(array $lifecycles, ?string $after, ?string $through = null): array;

    /** @return array<int, array<string, mixed>> */
    public function desired(): array;

    public function attempted(AccountData $account): void;

    public function succeeded(AccountData $account): void;

    public function failed(AccountData $account, string $code, int $failures, ?int $delay): void;
}
