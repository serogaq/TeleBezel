<?php

declare(strict_types=1);

namespace App\Console\Commands;

use App\Contracts\Repositories\TelegramAccountRepository;
use Illuminate\Console\Command;
use Illuminate\Support\Str;

final class UnblockAccounts extends Command
{
    protected $signature = 'telebezel:accounts-unblock {account? : Account UUID; all blocked accounts when omitted}';

    protected $description = 'Clear a permanent reconciliation block after its cause has been fixed';

    public function handle(TelegramAccountRepository $accounts): int
    {
        $account = $this->argument('account');
        if ($account !== null && ! Str::isUuid($account)) {
            $this->components->error('The account must be a UUID.');

            return self::FAILURE;
        }
        $ids = $accounts->unblock($account);
        $this->components->info(count($ids).' account(s) will be reconciled on the next pass.');

        return self::SUCCESS;
    }
}
