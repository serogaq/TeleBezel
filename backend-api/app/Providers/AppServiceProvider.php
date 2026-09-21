<?php

namespace App\Providers;

use App\Contracts\MonotonicClock;
use App\Contracts\Repositories\AdministrationRepository as AdministrationRepositoryContract;
use App\Contracts\Repositories\AuthenticationRepository as AuthenticationRepositoryContract;
use App\Contracts\Repositories\DeviceRepository as DeviceRepositoryContract;
use App\Contracts\Repositories\HealthRepository as HealthRepositoryContract;
use App\Contracts\Repositories\OwnerAccessRepository as OwnerAccessRepositoryContract;
use App\Contracts\Repositories\ProxyProfileRepository as ProxyProfileRepositoryContract;
use App\Contracts\Repositories\QuickReplyRepository as QuickReplyRepositoryContract;
use App\Contracts\Repositories\SchedulerRepository as SchedulerRepositoryContract;
use App\Contracts\Repositories\SettingsRepository as SettingsRepositoryContract;
use App\Contracts\Repositories\TelegramAccountRepository as TelegramAccountRepositoryContract;
use App\Contracts\TdlibGateway;
use App\Contracts\TdlibStatusClient;
use App\Contracts\TransactionManager;
use App\Infrastructure\Persistence\DatabaseTransactionManager;
use App\Infrastructure\Tdlib\TdlibGateway as TdlibGatewayImplementation;
use App\Infrastructure\Tdlib\TdlibStatusClient as TdlibStatusClientImplementation;
use App\Infrastructure\Time\SystemMonotonicClock;
use App\Repositories\AdministrationRepository;
use App\Repositories\AuthenticationRepository;
use App\Repositories\DeviceRepository;
use App\Repositories\HealthRepository;
use App\Repositories\OwnerAccessRepository;
use App\Repositories\ProxyProfileRepository;
use App\Repositories\QuickReplyRepository;
use App\Repositories\SchedulerRepository;
use App\Repositories\SettingsRepository;
use App\Repositories\TelegramAccountRepository;
use Illuminate\Cache\RateLimiting\Limit;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\RateLimiter;
use Illuminate\Support\ServiceProvider;

class AppServiceProvider extends ServiceProvider
{
    /**
     * Register any application services.
     */
    public function register(): void
    {
        $this->app->bind(TransactionManager::class, DatabaseTransactionManager::class);
        $this->app->bind(AdministrationRepositoryContract::class, AdministrationRepository::class);
        $this->app->bind(AuthenticationRepositoryContract::class, AuthenticationRepository::class);
        $this->app->bind(DeviceRepositoryContract::class, DeviceRepository::class);
        $this->app->bind(HealthRepositoryContract::class, HealthRepository::class);
        $this->app->bind(OwnerAccessRepositoryContract::class, OwnerAccessRepository::class);
        $this->app->bind(ProxyProfileRepositoryContract::class, ProxyProfileRepository::class);
        $this->app->bind(QuickReplyRepositoryContract::class, QuickReplyRepository::class);
        $this->app->bind(SchedulerRepositoryContract::class, SchedulerRepository::class);
        $this->app->bind(SettingsRepositoryContract::class, SettingsRepository::class);
        $this->app->bind(TelegramAccountRepositoryContract::class, TelegramAccountRepository::class);
        $this->app->bind(MonotonicClock::class, SystemMonotonicClock::class);
        $this->app->bind(TdlibGateway::class, TdlibGatewayImplementation::class);
        $this->app->bind(TdlibStatusClient::class, TdlibStatusClientImplementation::class);
    }

    /**
     * Bootstrap any application services.
     */
    public function boot(): void
    {
        RateLimiter::for('bootstrap', fn (Request $request) => Limit::perMinute(5)->by($request->ip()));
        RateLimiter::for('login', fn (Request $request) => Limit::perMinute(10)->by($request->ip()));
        RateLimiter::for('recovery', fn (Request $request) => Limit::perHour(5)->by($request->ip()));
    }
}
