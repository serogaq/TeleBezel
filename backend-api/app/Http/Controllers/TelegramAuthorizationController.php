<?php

namespace App\Http\Controllers;

use App\Http\Requests\AuthorizationActionRequest;
use App\Services\TdlibGateway;
use App\Services\TelegramAccountService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;

final class TelegramAuthorizationController extends Controller
{
    public function show(string $uuid, Request $request, TelegramAccountService $accounts, TdlibGateway $tdlib): JsonResponse
    {
        $accounts->find($uuid, false, $this->requestId($request));
        $data = $tdlib->snapshot($uuid, $this->requestId($request));
        $authorization = $data['authorization'] ?? [];

        return response()->json(['data' => is_array($authorization) ? $authorization : [], 'request_id' => $this->requestId($request)], 200, ['Cache-Control' => 'no-store']);
    }

    public function action(string $uuid, AuthorizationActionRequest $request, TelegramAccountService $accounts): JsonResponse
    {
        $data = $accounts->authorizationAction($uuid, $request->validated(), $this->requestId($request));
        $authorization = $data['authorization'] ?? [];

        return response()->json(['data' => is_array($authorization) ? $authorization : [], 'request_id' => $this->requestId($request)], 202, ['Cache-Control' => 'no-store']);
    }

    private function requestId(Request $request): string
    {
        return (string) $request->attributes->get('request_id');
    }
}
