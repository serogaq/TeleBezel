<?php

declare(strict_types=1);

namespace App\Services;

use App\Cache\HealthCacheKeys;
use App\Contracts\Repositories\HealthRepository;
use App\Contracts\TdlibStatusClient;
use App\Exceptions\ApiException;
use Illuminate\Database\QueryException;
use Illuminate\Support\Facades\Cache;
use RuntimeException;
use Throwable;

final readonly class HealthService
{
    public function __construct(private HealthRepository $database, private TdlibStatusClient $tdlib) {}

    public const READINESS_CACHE_SECONDS = 5;

    public function ready(): bool
    {
        try {
            return Cache::remember(HealthCacheKeys::readiness(), self::READINESS_CACHE_SECONDS, fn (): bool => $this->probe());
        } catch (Throwable) {
            return false;
        }
    }

    private function probe(): bool
    {
        try {
            $this->status();

            return true;
        } catch (Throwable) {
            return false;
        }
    }

    /** @return array{status: string, api_version: string, dependencies: array{postgres: string, tdlib: string}, tdlib_version: string} */
    public function status(): array
    {
        try {
            $this->database->check();
        } catch (QueryException) {
            throw new ApiException('service.database_unavailable', 503);
        }
        try {
            $status = $this->tdlib->status();
        } catch (RuntimeException) {
            throw new ApiException('service.tdlib_unavailable', 503);
        }

        return [
            'status' => 'ready',
            'api_version' => config()->string('telebezel.version'),
            'dependencies' => [
                'postgres' => 'ready',
                'tdlib' => 'ready',
            ],
            'tdlib_version' => $status['tdlib_version'],
        ];
    }
}
