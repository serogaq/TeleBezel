<?php

namespace App\Http\Controllers;

use App\Services\TdlibStatusClient;
use Illuminate\Http\JsonResponse;
use Illuminate\Support\Facades\DB;
use Throwable;

final class HealthController extends Controller
{
    public function live(): JsonResponse
    {
        return response()->json([
            'status' => 'ok',
            'service' => 'backend-api',
            'version' => config('telebezel.version'),
        ]);
    }

    public function ready(TdlibStatusClient $tdlib): JsonResponse
    {
        try {
            DB::selectOne('select 1 as ready');
            $tdlib->status();
        } catch (Throwable) {
            return response()->json(['status' => 'not_ready'], 503, ['Cache-Control' => 'no-store']);
        }

        return response()->json(['status' => 'ready'], 200, ['Cache-Control' => 'no-store']);
    }
}
