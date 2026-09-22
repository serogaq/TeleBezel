<?php

declare(strict_types=1);

namespace App\Http\Resources;

use App\Data\AccountData;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Illuminate\Pagination\LengthAwarePaginator;

final class AccountPageResource
{
    /** @param LengthAwarePaginator<int, AccountData> $page */
    public function __construct(private readonly LengthAwarePaginator $page) {}

    public function respond(Request $request, string $requestId): JsonResponse
    {
        return response()->json([
            'data' => $this->page->getCollection()->map(fn (AccountData $account): array => (new TelegramAccountResource($account))->resolve($request))->values()->all(),
            'pagination' => [
                'current_page' => $this->page->currentPage(),
                'per_page' => $this->page->perPage(),
                'total' => $this->page->total(),
                'last_page' => $this->page->lastPage(),
            ],
            'request_id' => $requestId,
        ], 200, [
            'Cache-Control' => 'no-store',
        ]);
    }
}
