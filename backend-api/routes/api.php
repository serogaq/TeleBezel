<?php

use App\Http\Controllers\HealthController;
use App\Http\Controllers\StatusController;
use Illuminate\Support\Facades\Route;

Route::get('/healthz', [HealthController::class, 'live']);
Route::get('/readyz', [HealthController::class, 'ready']);
Route::middleware(['api-client', 'api-client-rate-limit'])->get('/v1/status', StatusController::class);
