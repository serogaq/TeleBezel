<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::dropIfExists('configuration_launches');
    }

    public function down(): void
    {
        Schema::create('configuration_launches', function (Blueprint $table): void {
            $table->uuid('id')->primary();
            $table->uuid('device_id');
            $table->char('ticket_hash', 64)->unique();
            $table->string('callback_state', 128);
            $table->timestampTz('expires_at');
            $table->timestampTz('consumed_at')->nullable();
            $table->timestampsTz();
            $table->foreign('device_id')->references('id')->on('devices')->cascadeOnDelete();
        });
    }
};
