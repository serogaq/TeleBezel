<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Support\Facades\DB;

return new class extends Migration
{
    public function up(): void
    {
        DB::statement('ALTER TABLE instances ALTER COLUMN global_proxy TYPE text USING global_proxy::text');
    }

    public function down(): void
    {
        DB::statement('ALTER TABLE instances ALTER COLUMN global_proxy TYPE json USING to_json(global_proxy)');
    }
};
