<?php

arch('HTTP adapters cannot use persistence or the TDLib transport')
    ->expect('App\\Http\\Controllers')
    ->not->toUse(['App\\Models', 'App\\Repositories', 'Illuminate\\Support\\Facades\\DB', 'App\\Infrastructure']);

arch('application services cannot use Eloquent or HTTP requests')
    ->expect('App\\Services')
    ->not->toUse(['App\\Models', 'App\\Repositories', 'Illuminate\\Database\\Eloquent', 'Illuminate\\Http\\Request', 'App\\Infrastructure', 'Illuminate\\Support\\Facades\\DB']);

arch('middleware and resources cannot query models')
    ->expect(['App\\Http\\Middleware', 'App\\Http\\Resources'])
    ->not->toUse(['App\\Models', 'App\\Repositories', 'Illuminate\\Support\\Facades\\DB']);

arch('repositories do not authenticate passwords or communicate with TDLib')
    ->expect('App\\Repositories')
    ->not->toUse(['Illuminate\\Support\\Facades\\Hash', 'Illuminate\\Contracts\\Hashing\\Hasher', 'App\\Contracts\\TdlibGateway', 'Illuminate\\Support\\Facades\\Http']);
