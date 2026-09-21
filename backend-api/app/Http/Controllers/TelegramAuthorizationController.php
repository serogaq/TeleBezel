<?php

declare(strict_types=1);

namespace App\Http\Controllers;

use App\Http\RequestContext;
use App\Http\Requests\AuthorizationActionRequest;
use App\Http\Resources\AuthorizationResource;
use App\Services\TelegramAuthorizationService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;

final class TelegramAuthorizationController extends Controller
{
    public function show(string $uuid, Request $request, TelegramAuthorizationService $service): JsonResponse
    {
        return (new AuthorizationResource($service->show($uuid, RequestContext::requestId($request))))->respond(RequestContext::requestId($request));
    }

    public function action(string $uuid, AuthorizationActionRequest $request, TelegramAuthorizationService $service): JsonResponse
    {
        return (new AuthorizationResource($service->action($uuid, $request->inputData(), RequestContext::requestId($request))))->respond(RequestContext::requestId($request), 202);
    }
}
