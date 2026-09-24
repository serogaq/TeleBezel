<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::table('telegram_accounts', function (Blueprint $table): void {
            $table->index(['lifecycle', 'id']);
        });
        Schema::table('bootstrap_codes', function (Blueprint $table): void {
            $table->index('expires_at');
        });
    }

    public function down(): void
    {
        Schema::table('bootstrap_codes', function (Blueprint $table): void {
            $table->dropIndex(['expires_at']);
        });
        Schema::table('telegram_accounts', function (Blueprint $table): void {
            $table->dropIndex(['lifecycle', 'id']);
        });
    }
};
