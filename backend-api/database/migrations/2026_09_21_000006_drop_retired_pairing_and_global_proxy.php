<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::dropIfExists('pairing_requests');
        Schema::table('instances', function (Blueprint $table): void {
            $table->dropColumn('global_proxy');
        });
    }

    public function down(): void
    {
        Schema::table('instances', function (Blueprint $table): void {
            $table->text('global_proxy')->nullable();
        });
        Schema::create('pairing_requests', function (Blueprint $table): void {
            $table->uuid('id')->primary();
            $table->uuid('instance_id');
            $table->char('exchange_secret_hash', 64);
            $table->char('page_ticket_hash', 64)->unique();
            $table->string('callback_state', 128);
            $table->string('origin', 2048);
            $table->string('device_name', 100);
            $table->timestampTz('expires_at');
            $table->timestampTz('ticket_consumed_at')->nullable();
            $table->timestampTz('approved_at')->nullable();
            $table->uuid('device_id')->nullable();
            $table->text('exchange_result')->nullable();
            $table->timestampTz('exchange_expires_at')->nullable();
            $table->timestampTz('confirmed_at')->nullable();
            $table->timestampsTz();
            $table->foreign('instance_id')->references('id')->on('instances')->cascadeOnDelete();
            $table->foreign('device_id')->references('id')->on('devices')->nullOnDelete();
        });
    }
};
