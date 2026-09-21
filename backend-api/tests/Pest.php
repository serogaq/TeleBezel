<?php

use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Http;
use Tests\TestCase;

pest()->extend(TestCase::class)->beforeEach(fn () => Http::preventStrayRequests())->in('Feature');
pest()->use(RefreshDatabase::class)->in(
    'Feature/HealthTest.php', 'Feature/ApiClientCommandTest.php', 'Feature/StatusTest.php',
    'Feature/TelegramAccountsTest.php', 'Feature/OwnerAccessTest.php',
);

pest()->extend(TestCase::class)->use(RefreshDatabase::class)->in('Integration');

function tdlibReadyStatusFixture(): array
{
    return json_decode((string) file_get_contents(base_path('../backend-tdlib/tests/contracts/tdlib_status_ready.json')), true, flags: JSON_THROW_ON_ERROR);
}
