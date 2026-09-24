<?php

declare(strict_types=1);

namespace App\Repositories;

use App\Contracts\Repositories\QuickReplyRepository as QuickReplyRepositoryContract;
use App\Exceptions\ApiException;
use App\Models\Instance;
use App\Support\Values;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Str;
use stdClass;

final class QuickReplyRepository implements QuickReplyRepositoryContract
{
    public function lockInstance(string $instanceId): void
    {
        Instance::query()->whereKey($instanceId)->lockForUpdate()->firstOrFail();
    }

    /** @return array<int, array{id: string, text: string, position: int}> */
    public function all(string $instanceId): array
    {
        return DB::table('quick_replies')->where('instance_id', $instanceId)->orderBy('position')->get(['id', 'text', 'position'])->map(fn (stdClass $row): array => [
            'id' => Values::string($row->id),
            'text' => Values::string($row->text),
            'position' => Values::integer($row->position),
        ])->values()->all();
    }

    /** @return array{id: string, text: string, position: int} */
    public function create(string $instanceId, string $text): array
    {
        return DB::transaction(function () use ($instanceId, $text): array {
            Instance::query()->whereKey($instanceId)->lockForUpdate()->firstOrFail();
            $max = DB::table('quick_replies')->where('instance_id', $instanceId)->max('position');
            $position = $max === null ? 0 : Values::integer($max) + 1;
            $data = [
                'id' => (string) Str::uuid(),
                'text' => $text,
                'position' => $position,
            ];
            DB::table('quick_replies')->insert([
                ...$data,
                'instance_id' => $instanceId,
                'created_at' => now(),
                'updated_at' => now(),
            ]);

            return $data;
        }, 3);
    }

    /** @param list<string> $ids */
    public function reorder(string $instanceId, array $ids): void
    {
        DB::transaction(function () use ($instanceId, $ids): void {
            Instance::query()->whereKey($instanceId)->lockForUpdate()->firstOrFail();
            $max = DB::table('quick_replies')->where('instance_id', $instanceId)->max('position');
            $offset = $max === null ? 1 : Values::integer($max) + 1;
            foreach ([$offset, 0] as $base) {
                foreach ($ids as $position => $id) {
                    DB::table('quick_replies')->where('instance_id', $instanceId)->where('id', $id)->update([
                        'position' => $base + $position,
                        'updated_at' => now(),
                    ]);
                }
            }
        }, 3);
    }

    public function update(string $instanceId, string $id, string $text): void
    {
        if (DB::table('quick_replies')->where('instance_id', $instanceId)->where('id', $id)->update([
            'text' => $text,
            'updated_at' => now(),
        ]) === 0) {
            throw new ApiException('quick_replies.not_found', 404);
        }
    }

    public function delete(string $instanceId, string $id): bool
    {
        return DB::table('quick_replies')->where('instance_id', $instanceId)->where('id', $id)->delete() > 0;
    }

    public function revision(string $instanceId): int
    {
        return Values::integer(Instance::query()->whereKey($instanceId)->value('quick_replies_revision') ?? 1);
    }

    public function bumpRevision(string $instanceId): void
    {
        Instance::query()->whereKey($instanceId)->increment('quick_replies_revision');
    }
}
