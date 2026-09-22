<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::table('telegram_accounts', function (Blueprint $table): void {
            // The per-account proxy configuration, credentials included, so a
            // later reconciliation can restate the same intent instead of a
            // truncated one.
            $table->text('proxy_config')->nullable();
            $table->unsignedBigInteger('proxy_config_version')->default(0);
        });
        DB::statement('ALTER TABLE telegram_accounts ADD CONSTRAINT telegram_accounts_proxy_config_check CHECK ((proxy_id IS NULL AND proxy_config IS NULL AND proxy_config_version = 0) OR (proxy_id IS NOT NULL AND proxy_config IS NOT NULL AND proxy_config_version > 0))');
        DB::statement("ALTER TABLE telegram_accounts ADD CONSTRAINT telegram_accounts_removed_proxy_config_check CHECK (lifecycle != 'removed' OR proxy_config IS NULL)");
    }

    public function down(): void
    {
        DB::statement('ALTER TABLE telegram_accounts DROP CONSTRAINT IF EXISTS telegram_accounts_removed_proxy_config_check');
        DB::statement('ALTER TABLE telegram_accounts DROP CONSTRAINT IF EXISTS telegram_accounts_proxy_config_check');
        Schema::table('telegram_accounts', function (Blueprint $table): void {
            $table->dropColumn(['proxy_config', 'proxy_config_version']);
        });
    }
};
