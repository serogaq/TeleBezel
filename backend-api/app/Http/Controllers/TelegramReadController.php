<?php

declare(strict_types=1);

namespace App\Http\Controllers;

use App\Http\RequestContext;
use App\Http\Requests\InterestRequest;
use App\Http\Requests\ListChatsRequest;
use App\Http\Requests\ListMessagesRequest;
use App\Http\Requests\ListUpdatesRequest;
use App\Http\Requests\PreviewRequest;
use App\Http\Requests\ReadChatRequest;
use App\Http\Resources\TelegramReadResource;
use App\Services\TelegramReadService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Response;

final class TelegramReadController extends Controller
{
    public function chats(string $uuid, ListChatsRequest $request, TelegramReadService $service): JsonResponse
    {
        return (new TelegramReadResource($service->chats($uuid, $request->inputData(), RequestContext::requestId($request)), 'chats'))->respond(RequestContext::requestId($request));
    }

    public function chat(string $uuid, string $chatId, ReadChatRequest $request, TelegramReadService $service): JsonResponse
    {
        return (new TelegramReadResource($service->chat($uuid, $chatId, $request->inputData(), RequestContext::principal($request), RequestContext::requestId($request)), 'chat'))->respond(RequestContext::requestId($request));
    }

    public function messages(string $uuid, string $chatId, ListMessagesRequest $request, TelegramReadService $service): JsonResponse
    {
        return (new TelegramReadResource($service->messages($uuid, $chatId, $request->inputData(), RequestContext::principal($request), RequestContext::requestId($request)), 'messages'))->respond(RequestContext::requestId($request));
    }

    public function message(string $uuid, string $chatId, string $messageId, ReadChatRequest $request, TelegramReadService $service): JsonResponse
    {
        return (new TelegramReadResource($service->message($uuid, $chatId, $messageId, $request->inputData(), RequestContext::principal($request), RequestContext::requestId($request)), 'message'))->respond(RequestContext::requestId($request));
    }

    public function preview(string $uuid, string $chatId, string $messageId, string $previewId, PreviewRequest $request, TelegramReadService $service): Response
    {
        $preview = $service->preview($uuid, $chatId, $messageId, $previewId, RequestContext::requestId($request));

        return response($preview['bytes'], 200, [
            'Content-Type' => $preview['mime_type'],
            'Cache-Control' => 'private, no-store',
            'X-Content-Type-Options' => 'nosniff',
        ]);
    }

    public function updates(string $uuid, ListUpdatesRequest $request, TelegramReadService $service): JsonResponse
    {
        return (new TelegramReadResource($service->updates($uuid, $request->inputData(), RequestContext::principal($request), RequestContext::requestId($request)), 'updates'))->respond(RequestContext::requestId($request));
    }

    public function putInterest(string $uuid, string $chatId, string $viewId, InterestRequest $request, TelegramReadService $service): JsonResponse
    {
        return (new TelegramReadResource($service->interest(true, $uuid, $chatId, $viewId, RequestContext::principal($request), RequestContext::requestId($request)), 'interest'))->respond(RequestContext::requestId($request));
    }

    public function deleteInterest(string $uuid, string $chatId, string $viewId, InterestRequest $request, TelegramReadService $service): JsonResponse
    {
        return (new TelegramReadResource($service->interest(false, $uuid, $chatId, $viewId, RequestContext::principal($request), RequestContext::requestId($request)), 'interest'))->respond(RequestContext::requestId($request));
    }
}
