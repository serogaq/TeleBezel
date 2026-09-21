<?php

namespace App\Http\Controllers;

use App\Exceptions\ApiException;
use App\Services\TdlibGateway;
use App\Services\TelegramAccountService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;

final class TelegramReadController extends Controller
{
    public function chats(string $uuid, Request $request, TelegramAccountService $accounts, TdlibGateway $tdlib): JsonResponse
    {
        $accounts->find($uuid, false, $this->requestId($request));
        $query = $request->validate(['list' => ['sometimes', 'in:main,archive'], 'limit' => ['sometimes', 'integer', 'between:1,50'], 'cursor' => ['sometimes', 'string', 'max:4096']]);

        return $this->result($tdlib->chats($uuid, $query + ['list' => 'main', 'limit' => 20], $this->requestId($request)), $request);
    }

    public function chat(string $uuid, string $chatId, Request $request, TelegramAccountService $accounts, TdlibGateway $tdlib): JsonResponse
    {
        $this->validateId($chatId, 'chat.not_found');
        $accounts->find($uuid, false, $this->requestId($request));

        return $this->result($tdlib->chat($uuid, $chatId, $this->leaseQuery($request), $this->requestId($request)), $request);
    }

    public function messages(string $uuid, string $chatId, Request $request, TelegramAccountService $accounts, TdlibGateway $tdlib): JsonResponse
    {
        $this->validateId($chatId, 'chat.not_found');
        $accounts->find($uuid, false, $this->requestId($request));
        $query = $request->validate(['view_id' => ['required', 'uuid'], 'limit' => ['sometimes', 'integer', 'between:1,50'],
            'cursor' => ['sometimes', 'string', 'max:4096'], 'retry_cursor' => ['sometimes', 'string', 'max:4096']]);

        return $this->result($tdlib->messages($uuid, $chatId, $query + $this->principal($request) + ['limit' => 30], $this->requestId($request)), $request);
    }

    public function message(string $uuid, string $chatId, string $messageId, Request $request, TelegramAccountService $accounts, TdlibGateway $tdlib): JsonResponse
    {
        $this->validateId($chatId, 'chat.not_found');
        $this->validateId($messageId, 'message.not_found');
        $accounts->find($uuid, false, $this->requestId($request));

        return $this->result($tdlib->message($uuid, $chatId, $messageId, $this->leaseQuery($request), $this->requestId($request)), $request);
    }

    public function updates(string $uuid, Request $request, TelegramAccountService $accounts, TdlibGateway $tdlib): JsonResponse
    {
        $accounts->find($uuid, false, $this->requestId($request));
        $query = $request->validate(['cursor' => ['sometimes', 'string', 'max:4096'], 'limit' => ['sometimes', 'integer', 'between:1,100']]);

        return $this->result($tdlib->updates($uuid, $query + ['limit' => 100], $this->requestId($request)), $request);
    }

    public function putInterest(string $uuid, string $chatId, string $viewId, Request $request, TelegramAccountService $accounts, TdlibGateway $tdlib): JsonResponse
    {
        return $this->interest('PUT', $uuid, $chatId, $viewId, $request, $accounts, $tdlib);
    }

    public function deleteInterest(string $uuid, string $chatId, string $viewId, Request $request, TelegramAccountService $accounts, TdlibGateway $tdlib): JsonResponse
    {
        return $this->interest('DELETE', $uuid, $chatId, $viewId, $request, $accounts, $tdlib);
    }

    private function interest(string $method, string $uuid, string $chatId, string $viewId, Request $request, TelegramAccountService $accounts, TdlibGateway $tdlib): JsonResponse
    {
        $this->validateId($chatId, 'chat.not_found');
        if (preg_match('/^[0-9a-f-]{36}$/i', $viewId) !== 1) {
            throw new ApiException('request.invalid', 422);
        }
        $accounts->find($uuid, false, $this->requestId($request));

        return $this->result($tdlib->interest($method, $uuid, $chatId, $viewId, $this->principal($request), $this->requestId($request)), $request);
    }

    /** @return array<string, mixed> */
    private function leaseQuery(Request $request): array
    {
        $data = $request->validate(['view_id' => ['required', 'uuid']]);

        return $data + $this->principal($request);
    }

    /** @return array<string, mixed> */
    private function principal(Request $request): array
    {
        return ['principal_type' => $request->attributes->get('principal_type'), 'principal_id' => $request->attributes->get('principal_id')];
    }

    private function validateId(string $id, string $code): void
    {
        if (preg_match('/^-?[1-9][0-9]{0,19}$/', $id) !== 1) {
            throw new ApiException($code, 404);
        }
    }

    /** @param array<string, mixed> $data */
    private function result(array $data, Request $request): JsonResponse
    {
        return response()->json(['data' => $data, 'request_id' => $this->requestId($request)], 200, ['Cache-Control' => 'no-store']);
    }

    private function requestId(Request $request): string
    {
        return (string) $request->attributes->get('request_id');
    }
}
