<?php

namespace App\Http\Controllers;

use App\Services\TdlibStatusClient;
use Illuminate\Database\QueryException;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\DB;
use RuntimeException;

final class StatusController extends Controller
{
    public function __invoke(Request $request, TdlibStatusClient $tdlib): JsonResponse
    {
        try {
            DB::selectOne('select 1 as ready');
        } catch (QueryException) {
            return $this->error($request, 'service.database_unavailable');
        }

        try {
            $tdlibStatus = $tdlib->status();
        } catch (RuntimeException) {
            return $this->error($request, 'service.tdlib_unavailable');
        }

        return response()->json([
            'data' => [
                'status' => 'ready',
                'api_version' => config('telebezel.version'),
                'dependencies' => ['postgres' => 'ready', 'tdlib' => 'ready'],
                'tdlib_version' => $tdlibStatus['tdlib_version'],
            ],
            'request_id' => $request->attributes->get('request_id'),
        ], 200, ['Cache-Control' => 'no-store']);
    }

    private function error(Request $request, string $code): JsonResponse
    {
        return response()->json([
            'error' => ['code' => $code],
            'request_id' => $request->attributes->get('request_id'),
        ], 503, ['Cache-Control' => 'no-store']);
    }
}
