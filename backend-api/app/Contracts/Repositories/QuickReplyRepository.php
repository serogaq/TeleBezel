<?php

declare(strict_types=1);

namespace App\Contracts\Repositories;

interface QuickReplyRepository
{
    public function lockInstance(string $instanceId): void;

    /** @return array<int, array{id: string, text: string, position: int}> */
    public function all(string $instanceId): array;

    /** @return array{id: string, text: string, position: int} */
    public function create(string $instanceId, string $text): array;

    /** @param list<string> $ids */
    public function reorder(string $instanceId, array $ids): void;

    public function update(string $instanceId, string $id, string $text): void;

    public function delete(string $instanceId, string $id): void;
}
