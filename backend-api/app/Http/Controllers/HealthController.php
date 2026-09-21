<?php

declare(strict_types=1);

namespace App\Http\Controllers;

use App\Http\Resources\HealthResource;
use App\Services\HealthService;
use Illuminate\Http\JsonResponse;

final class HealthController extends Controller
{
    public function live(): JsonResponse
    {
        return HealthResource::live();
    }

    public function ready(HealthService $health): JsonResponse
    {
        return HealthResource::readiness($health->ready());
    }
}
