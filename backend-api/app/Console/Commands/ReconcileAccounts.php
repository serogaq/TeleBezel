<?php

namespace App\Console\Commands;

use App\Enums\AccountLifecycle;
use App\Exceptions\ApiException;
use App\Models\TelegramAccount;
use App\Services\TdlibGateway;
use App\Services\TelegramAccountService;
use Illuminate\Console\Command;
use Illuminate\Support\Facades\Cache;
use Illuminate\Support\Str;

final class ReconcileAccounts extends Command
{
    protected $signature = 'telebezel:accounts-reconcile {--dry-run : Report desired accounts without activating clients}';

    protected $description = 'Reconcile durable Telegram account intent with the TDLib runtime';

    public function handle(TelegramAccountService $accounts, TdlibGateway $tdlib): int
    {
        $lock = Cache::lock('telebezel:accounts-reconcile', 50);
        if (! $lock->get()) {
            $this->warn('Another reconciliation pass is active.');

            return self::SUCCESS;
        }
        $started = hrtime(true);
        $requestId = (string) Str::uuid();
        try {
            $query = TelegramAccount::withTrashed()
                ->where('lifecycle', '!=', AccountLifecycle::Removed->value);
            if ($this->option('dry-run')) {
                $local = (clone $query)->orderBy('id')->get(['id', 'storage_generation', 'lifecycle', 'desired_revision']);
                $remote = $tdlib->listSnapshots([], $requestId);
                $this->line(json_encode(['desired' => $local->toArray(), 'runtime' => $remote], JSON_THROW_ON_ERROR | JSON_UNESCAPED_SLASHES));

                return self::SUCCESS;
            }
            $phases = [
                [AccountLifecycle::LogoutPending->value, AccountLifecycle::Removing->value, AccountLifecycle::Provisioning->value],
                [AccountLifecycle::Active->value],
            ];
            foreach ($phases as $lifecycles) {
                $continue = (clone $query)->whereIn('lifecycle', $lifecycles)->chunkById(25, function ($batch) use ($accounts, $requestId, $started): bool {
                    foreach ($batch as $account) {
                        if ((hrtime(true) - $started) / 1_000_000_000 >= 45) {
                            return false;
                        }
                        try {
                            $accounts->reconcile($account, $requestId);
                        } catch (ApiException $exception) {
                            $this->warn("{$account->id}: {$exception->errorCode}");
                        }
                    }

                    return true;
                }, 'id');
                if ($continue === false || (hrtime(true) - $started) / 1_000_000_000 >= 45) {
                    break;
                }
            }
        } finally {
            $lock->release();
        }

        return self::SUCCESS;
    }
}
