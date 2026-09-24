<?php

declare(strict_types=1);

namespace App\Http\Controllers;

use App\Http\RequestContext;
use App\Http\Requests\ReorderQuickRepliesRequest;
use App\Http\Requests\WriteQuickReplyRequest;
use App\Http\Resources\ActionResource;
use App\Http\Resources\QuickReplyResource;
use App\Services\QuickReplyService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;

final class QuickReplyController extends Controller
{
    public function index(Request $request, QuickReplyService $replies): JsonResponse
    {
        return QuickReplyResource::snapshot($replies->snapshot(RequestContext::instanceId($request)))->respond(RequestContext::requestId($request));
    }

    public function store(WriteQuickReplyRequest $request, QuickReplyService $replies): JsonResponse
    {
        return (new QuickReplyResource($replies->create(RequestContext::instanceId($request), $request->inputData()->string('text'))))->respond(status: 201);
    }

    public function reorder(ReorderQuickRepliesRequest $request, QuickReplyService $replies): JsonResponse
    {
        return QuickReplyResource::listing($replies->reorder(RequestContext::instanceId($request), $request->inputData()->strings('ids')))->respond(status: 200);
    }

    public function update(string $id, WriteQuickReplyRequest $request, QuickReplyService $replies): JsonResponse
    {
        return QuickReplyResource::listing($replies->update(RequestContext::instanceId($request), $id, $request->inputData()->string('text')))->respond(status: 200);
    }

    public function destroy(string $id, Request $request, QuickReplyService $replies): JsonResponse
    {
        $replies->delete(RequestContext::instanceId($request), $id);

        return (new ActionResource('deleted'))->respond();
    }
}
