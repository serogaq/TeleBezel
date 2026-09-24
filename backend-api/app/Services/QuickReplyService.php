<?php

declare(strict_types=1);

namespace App\Services;

use App\Contracts\Repositories\QuickReplyRepository;
use App\Contracts\TransactionManager;
use App\Exceptions\ApiException;

final readonly class QuickReplyService
{
    public const int LIMIT = 50;

    public function __construct(private QuickReplyRepository $replies, private TransactionManager $transactions) {}

    /** @return array<int, array{id: string, text: string, position: int}> */
    public function all(string $instanceId): array
    {
        return $this->replies->all($instanceId);
    }

    /** @return array{items: array<int, array{id: string, text: string, position: int}>, revision: int} */
    public function snapshot(string $instanceId): array
    {
        return $this->transactions->run(fn (): array => [
            'items' => $this->replies->all($instanceId),
            'revision' => $this->replies->revision($instanceId),
        ]);
    }

    /** @return array{id: string, text: string, position: int} */
    public function create(string $instanceId, string $text): array
    {
        return $this->transactions->run(function () use ($instanceId, $text): array {
            $this->replies->lockInstance($instanceId);
            if (count($this->replies->all($instanceId)) >= self::LIMIT) {
                throw new ApiException('quick_replies.limit_reached', 422);
            }
            $reply = $this->replies->create($instanceId, $text);
            $this->replies->bumpRevision($instanceId);

            return $reply;
        });
    }

    /** @param list<string> $ids
     * @return array<int, array{id: string, text: string, position: int}> */
    public function reorder(string $instanceId, array $ids): array
    {
        return $this->transactions->run(function () use ($instanceId, $ids): array {
            $this->replies->lockInstance($instanceId);
            $existing = array_column($this->replies->all($instanceId), 'id');
            $requested = $ids;
            sort($existing);
            sort($requested);
            if ($existing !== $requested) {
                throw new ApiException('operation.conflict', 409);
            }
            $this->replies->reorder($instanceId, $ids);
            $this->replies->bumpRevision($instanceId);

            return $this->all($instanceId);
        });
    }

    /** @return array<int, array{id: string, text: string, position: int}> */
    public function update(string $instanceId, string $id, string $text): array
    {
        return $this->transactions->run(function () use ($instanceId, $id, $text): array {
            $this->replies->lockInstance($instanceId);
            $this->replies->update($instanceId, $id, $text);
            $this->replies->bumpRevision($instanceId);

            return $this->all($instanceId);
        });
    }

    public function delete(string $instanceId, string $id): void
    {
        $this->transactions->run(function () use ($instanceId, $id): void {
            $this->replies->lockInstance($instanceId);
            if ($this->replies->delete($instanceId, $id)) {
                $this->replies->bumpRevision($instanceId);
            }
        });
    }
}
