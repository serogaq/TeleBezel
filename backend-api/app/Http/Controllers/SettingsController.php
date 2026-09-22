<?php

declare(strict_types=1);

namespace App\Http\Controllers;

use App\Http\RequestContext;
use App\Http\Requests\CreateDeviceRequest;
use App\Http\Requests\ReorderQuickRepliesRequest;
use App\Http\Requests\UpdateSettingsRequest;
use App\Http\Requests\WriteQuickReplyRequest;
use App\Http\Resources\ActionResource;
use App\Http\Resources\DeviceResource;
use App\Http\Resources\QuickReplyResource;
use App\Http\Resources\SettingsResource;
use App\Services\DeviceService;
use App\Services\QuickReplyService;
use App\Services\SettingsService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Illuminate\View\View;

final class SettingsController extends Controller
{
    public function page(): View
    {
        return view('settings');
    }

    public function show(Request $request, SettingsService $settings): JsonResponse
    {
        return (new SettingsResource($settings->show(RequestContext::instanceId($request))))->respond(status: 200);
    }

    public function update(UpdateSettingsRequest $request, SettingsService $settings): JsonResponse
    {
        return (new SettingsResource($settings->update(RequestContext::instanceId($request), $request->inputData(), RequestContext::requestId($request))))->respond(status: 200);
    }

    public function devices(Request $request, DeviceService $devices): JsonResponse
    {
        return DeviceResource::listing($devices->all(RequestContext::instanceId($request)))->respond(status: 200);
    }

    public function storeDevice(CreateDeviceRequest $request, DeviceService $devices): JsonResponse
    {
        return (new DeviceResource($devices->create(RequestContext::instanceId($request), $request->inputData()->string('name'))))->respond(status: 201);
    }

    public function quickReplies(Request $request, QuickReplyService $replies): JsonResponse
    {
        return QuickReplyResource::listing($replies->all(RequestContext::instanceId($request)))->respond(status: 200);
    }

    public function storeQuickReply(WriteQuickReplyRequest $request, QuickReplyService $replies): JsonResponse
    {
        return (new QuickReplyResource($replies->create(RequestContext::instanceId($request), $request->inputData()->string('text'))))->respond(status: 201);
    }

    public function reorderQuickReplies(ReorderQuickRepliesRequest $request, QuickReplyService $replies): JsonResponse
    {
        return QuickReplyResource::listing($replies->reorder(RequestContext::instanceId($request), $request->inputData()->strings('ids')))->respond(status: 200);
    }

    public function updateQuickReply(string $id, WriteQuickReplyRequest $request, QuickReplyService $replies): JsonResponse
    {
        return QuickReplyResource::listing($replies->update(RequestContext::instanceId($request), $id, $request->inputData()->string('text')))->respond(status: 200);
    }

    public function revokeDevice(string $id, Request $request, DeviceService $devices): JsonResponse
    {
        $devices->revoke(RequestContext::instanceId($request), $id, RequestContext::requestId($request));

        return (new ActionResource('revoked'))->respond();
    }

    public function destroyQuickReply(string $id, Request $request, QuickReplyService $replies): JsonResponse
    {
        $replies->delete(RequestContext::instanceId($request), $id);

        return (new ActionResource('deleted'))->respond();
    }
}
