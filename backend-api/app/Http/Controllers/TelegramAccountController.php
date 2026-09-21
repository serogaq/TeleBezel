<?php

namespace App\Http\Controllers;

use App\Http\RequestContext;
use App\Http\Requests\CreateTelegramAccountRequest;
use App\Http\Requests\DeleteTelegramAccountRequest;
use App\Http\Requests\ListAccountsRequest;
use App\Http\Requests\UpdateProxyRequest;
use App\Http\Resources\AccountPageResource;
use App\Http\Resources\AccountProxyResource;
use App\Http\Resources\TelegramAccountResource;
use App\Services\TelegramAccountService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Response;

final class TelegramAccountController extends Controller
{
    public function index(ListAccountsRequest $request, TelegramAccountService $accounts): JsonResponse
    {
        $page = $accounts->paginate($request->integer('per_page', 20), $this->requestId($request), $request->integer('page', 1));

        return (new AccountPageResource($page))->respond($request, $this->requestId($request));
    }

    public function store(CreateTelegramAccountRequest $request, TelegramAccountService $accounts): JsonResponse
    {
        $arguments = [RequestContext::principal($request)->id, (string) $request->header('Idempotency-Key'), $request->validated(), $this->requestId($request)];
        [$account, $created] = $request->attributes->get('principal_type') === 'owner' ? $accounts->createForOwner(...$arguments) : $accounts->create(...$arguments);

        return (new TelegramAccountResource($account))->respond($request, $this->requestId($request), $created ? 201 : 200);
    }

    public function show(string $uuid, Request $request, TelegramAccountService $accounts): JsonResponse
    {
        $account = $accounts->find($uuid, true, $this->requestId($request));

        return (new TelegramAccountResource($account))->respond($request, $this->requestId($request), 200);
    }

    public function proxy(string $uuid, Request $request, TelegramAccountService $accounts): JsonResponse
    {
        $account = $accounts->find($uuid, false, $this->requestId($request));

        return (new AccountProxyResource($accounts->proxy($account)))->respond($this->requestId($request));
    }

    public function updateProxy(string $uuid, UpdateProxyRequest $request, TelegramAccountService $accounts): JsonResponse
    {
        $account = $accounts->updateProxy($uuid, $request->validated(), $this->requestId($request));

        return (new TelegramAccountResource($account))->respond($request, $this->requestId($request), 202);
    }

    public function logout(string $uuid, Request $request, TelegramAccountService $accounts): JsonResponse
    {
        $account = $accounts->logout($uuid, $this->requestId($request));

        return (new TelegramAccountResource($account))->respond($request, $this->requestId($request), 202);
    }

    public function destroy(string $uuid, DeleteTelegramAccountRequest $request, TelegramAccountService $accounts): Response
    {
        $account = $accounts->remove($uuid, $this->requestId($request));
        if ($account === null) {
            return response()->noContent();
        }

        return (new TelegramAccountResource($account))->respond($request, $this->requestId($request), 202);
    }

    private function requestId(Request $request): string
    {
        return RequestContext::requestId($request);
    }
}
