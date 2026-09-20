<?php

namespace App\Http\Controllers;

use App\Http\Requests\CreateTelegramAccountRequest;
use App\Http\Requests\DeleteTelegramAccountRequest;
use App\Http\Requests\UpdateProxyRequest;
use App\Http\Resources\TelegramAccountResource;
use App\Services\TelegramAccountService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Response;

final class TelegramAccountController extends Controller
{
    public function index(Request $request, TelegramAccountService $accounts): JsonResponse
    {
        $page = $accounts->paginate((int) $request->integer('per_page', 20), $this->requestId($request));
        $data = $page->getCollection()->map(
            fn ($account): array => (new TelegramAccountResource($account))->resolve($request)
        )->values()->all();

        return response()->json([
            'data' => $data,
            'pagination' => [
                'current_page' => $page->currentPage(),
                'per_page' => $page->perPage(),
                'total' => $page->total(),
                'last_page' => $page->lastPage(),
            ],
            'request_id' => $this->requestId($request),
        ], 200, ['Cache-Control' => 'no-store']);
    }

    public function store(CreateTelegramAccountRequest $request, TelegramAccountService $accounts): JsonResponse
    {
        [$account, $created] = $accounts->create(
            (string) $request->attributes->get('api_client_id'),
            (string) $request->header('Idempotency-Key'),
            $request->validated(),
            $this->requestId($request),
        );

        return response()->json(['data' => (new TelegramAccountResource($account))->resolve($request), 'request_id' => $this->requestId($request)], $created ? 201 : 200, ['Cache-Control' => 'no-store']);
    }

    public function show(string $uuid, Request $request, TelegramAccountService $accounts): JsonResponse
    {
        $account = $accounts->find($uuid, true, $this->requestId($request));

        return response()->json(['data' => (new TelegramAccountResource($account))->resolve($request), 'request_id' => $this->requestId($request)], 200, ['Cache-Control' => 'no-store']);
    }

    public function proxy(string $uuid, Request $request, TelegramAccountService $accounts): JsonResponse
    {
        $account = $accounts->find($uuid, false, $this->requestId($request));

        return response()->json(['data' => $accounts->proxy($account), 'request_id' => $this->requestId($request)], 200, ['Cache-Control' => 'no-store']);
    }

    public function updateProxy(string $uuid, UpdateProxyRequest $request, TelegramAccountService $accounts): JsonResponse
    {
        $account = $accounts->updateProxy($uuid, $request->validated(), $this->requestId($request));

        return response()->json(['data' => (new TelegramAccountResource($account))->resolve($request), 'request_id' => $this->requestId($request)], 202, ['Cache-Control' => 'no-store']);
    }

    public function logout(string $uuid, Request $request, TelegramAccountService $accounts): JsonResponse
    {
        $account = $accounts->logout($uuid, $this->requestId($request));

        return response()->json(['data' => (new TelegramAccountResource($account))->resolve($request), 'request_id' => $this->requestId($request)], 202, ['Cache-Control' => 'no-store']);
    }

    public function destroy(string $uuid, DeleteTelegramAccountRequest $request, TelegramAccountService $accounts): Response
    {
        $account = $accounts->remove($uuid, $this->requestId($request));
        if ($account === null) {
            return response()->noContent();
        }

        return response()->json(['data' => (new TelegramAccountResource($account))->resolve($request), 'request_id' => $this->requestId($request)], 202, ['Cache-Control' => 'no-store']);
    }

    private function requestId(Request $request): string
    {
        return (string) $request->attributes->get('request_id');
    }
}
