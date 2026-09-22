<?php

declare(strict_types=1);

namespace App\Http\Controllers;

use App\Http\RequestContext;
use App\Http\Resources\ApiResource;
use App\Services\HealthService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;

final class StatusController extends Controller
{
    public function __invoke(Request $request, HealthService $health): JsonResponse
    {
        return (new ApiResource($health->status()))->respond(RequestContext::requestId($request));
    }
}
