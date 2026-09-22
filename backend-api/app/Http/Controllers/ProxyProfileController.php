<?php

declare(strict_types=1);

namespace App\Http\Controllers;

use App\Http\RequestContext;
use App\Http\Requests\ConfigureProxyProfilesRequest;
use App\Http\Requests\CreateProxyProfileRequest;
use App\Http\Resources\ProxyProfileResource;
use App\Http\Resources\ProxySettingsResource;
use App\Services\ProxyProfileService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Response;

final class ProxyProfileController extends Controller
{
    public function index(Request $request, ProxyProfileService $profiles): JsonResponse
    {
        return ProxyProfileResource::listing($profiles->all(RequestContext::instanceId($request)))->respond();
    }

    public function store(CreateProxyProfileRequest $request, ProxyProfileService $profiles): JsonResponse
    {
        return (new ProxyProfileResource($profiles->create(RequestContext::instanceId($request), $request->inputData(), RequestContext::requestId($request))))->respond(status: 201);
    }

    public function ping(string $id, Request $request, ProxyProfileService $profiles): JsonResponse
    {
        return (new ProxyProfileResource($profiles->ping(RequestContext::instanceId($request), $id, RequestContext::requestId($request))))->respond();
    }

    public function pingAll(Request $request, ProxyProfileService $profiles): JsonResponse
    {
        return ProxyProfileResource::listing($profiles->pingAll(RequestContext::instanceId($request), RequestContext::requestId($request)))->respond();
    }

    public function activate(string $id, Request $request, ProxyProfileService $profiles): JsonResponse
    {
        $profiles->activate(RequestContext::instanceId($request), $id, RequestContext::requestId($request));

        return ProxyProfileResource::listing($profiles->all(RequestContext::instanceId($request)))->respond();
    }

    public function direct(Request $request, ProxyProfileService $profiles): JsonResponse
    {
        $profiles->activate(RequestContext::instanceId($request), null, RequestContext::requestId($request));

        return ProxyProfileResource::listing($profiles->all(RequestContext::instanceId($request)))->respond();
    }

    public function configure(ConfigureProxyProfilesRequest $request, ProxyProfileService $profiles): JsonResponse
    {
        $profiles->configure(RequestContext::instanceId($request), $request->inputData());

        return (new ProxySettingsResource($request->inputData()->all()))->respond();
    }

    public function destroy(string $id, Request $request, ProxyProfileService $profiles): Response
    {
        $profiles->delete(RequestContext::instanceId($request), $id);

        return response()->noContent();
    }
}
