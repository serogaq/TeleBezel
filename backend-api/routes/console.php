<?php

use Illuminate\Support\Facades\Schedule;

Schedule::command('telebezel:accounts-reconcile')->everyMinute()->withoutOverlapping(1);
