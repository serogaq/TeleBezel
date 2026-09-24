<?php

declare(strict_types=1);

namespace App\Http\Controllers;

use App\Http\RequestContext;
use App\Http\Requests\SendMessageRequest;
use App\Http\Resources\SendResource;
use App\Services\MessageSendService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;

final class TelegramSendController extends Controller
{
    public function send(string $uuid, string $chatId, SendMessageRequest $request, MessageSendService $sends): JsonResponse
    {
        $input = $request->inputData();
        $send = $sends->send(RequestContext::principal($request), $uuid, $chatId, (string) $request->header('Idempotency-Key'), $input->string('text'), $input->nullableString('reply_to_message_id'), RequestContext::requestId($request));

        return (new SendResource($send))->respond(RequestContext::requestId($request), 202);
    }

    public function status(string $uuid, string $operationId, Request $request, MessageSendService $sends): JsonResponse
    {
        return (new SendResource($sends->status(RequestContext::principal($request), $uuid, $operationId, RequestContext::requestId($request))))->respond(RequestContext::requestId($request));
    }
}
