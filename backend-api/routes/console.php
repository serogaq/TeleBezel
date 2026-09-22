<?php

use Illuminate\Support\Facades\Schedule;

Schedule::command('telebezel:accounts-reconcile')->everyMinute()->withoutOverlapping(2);
Schedule::command('telebezel:proxy-monitor')->everyMinute()->withoutOverlapping(2);
Schedule::command('telebezel:purge-expired')->hourly()->withoutOverlapping(10);
