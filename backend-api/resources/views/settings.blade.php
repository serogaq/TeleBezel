<!doctype html>
<html lang="{{ $locale }}">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <meta name="csrf-token" content="{{ csrf_token() }}">
    <meta name="api-csrf" content="{{ $apiCsrf }}">
    <title>{{ $t['title'] }}</title>
    <link rel="stylesheet" href="/assets/settings.css">
</head>
<body>
<main>
    <header><p class="eyebrow">{{ $t['eyebrow'] }}</p><h1>TeleBezel</h1><label class="language">{{ $t['language'] }} <select id="language"><option value="en" @selected($locale === 'en')>English</option><option value="ru" @selected($locale === 'ru')>Русский</option></select></label><p id="status" role="status">{{ $t['status_intro'] }}</p></header>
    <section id="access" hidden><h2>{{ $t['owner_access'] }}</h2><div class="columns">
        <form id="login"><h3>{{ $t['sign_in'] }}</h3><label>{{ $t['password'] }} <input name="password" type="password" autocomplete="current-password" required></label><button>{{ $t['sign_in'] }}</button></form>
        <form id="bootstrap"><h3>{{ $t['first_setup'] }}</h3><label>{{ $t['bootstrap_code'] }} <input name="bootstrap_code" autocomplete="one-time-code" required></label><label>{{ $t['new_password'] }} <input name="password" type="password" minlength="12" autocomplete="new-password" required></label><button>{{ $t['create_owner'] }}</button></form>
        <form id="recover"><h3>{{ $t['recover_access'] }}</h3><label>{{ $t['recovery_code'] }} <input name="recovery_code" autocomplete="one-time-code" required></label><label>{{ $t['new_password'] }} <input name="password" type="password" minlength="12" autocomplete="new-password" required></label><button>{{ $t['reset_access'] }}</button></form>
    </div></section>
    <div id="configuration" hidden>
        <section><h2>{{ $t['status'] }}</h2><dl id="runtime-status"></dl></section>
        <section><h2>{{ $t['telegram_application'] }}</h2><form id="telegram"><label>{{ $t['api_id'] }} <input name="telegram_api_id" inputmode="numeric" required></label><label>{{ $t['api_hash'] }} <input name="telegram_api_hash" type="password" autocomplete="off" placeholder="{{ $t['keep_blank'] }}"></label><button>{{ $t['save_credentials'] }}</button></form></section>
        <section><h2>{{ $t['proxy_profiles'] }}</h2><p>{{ $t['proxy_intro'] }}</p><form id="proxy-add"><div class="columns"><label>{{ $t['name'] }} <input name="label" maxlength="100" required></label><label>{{ $t['mode'] }} <select name="mode"><option value="socks5">SOCKS5</option><option value="http">HTTP</option><option value="mtproto">MTProto</option></select></label></div><div class="columns"><label>{{ $t['host'] }} <input name="host" required></label><label>{{ $t['port'] }} <input name="port" inputmode="numeric" required></label></div><div class="columns"><label>{{ $t['username'] }} <input name="username" autocomplete="off"></label><label>{{ $t['password_secret'] }} <input name="credential" type="password" autocomplete="off"></label></div><label class="check"><input name="http_only" type="checkbox"> {{ $t['http_only'] }}</label><button>{{ $t['add_and_test'] }}</button></form><form id="proxy-policy" class="inline"><label>{{ $t['failure_action'] }} <select name="failure_action"><option value="next">{{ $t['failure_next'] }}</option><option value="stay">{{ $t['failure_stay'] }}</option><option value="direct">{{ $t['failure_direct'] }}</option></select></label><label>{{ $t['connect_timeout'] }} <input name="connect_timeout_seconds" type="number" min="3" max="300" value="10" required></label><button>{{ $t['save_policy'] }}</button></form><div class="inline"><button id="proxy-ping-all" type="button" class="secondary">{{ $t['ping_all'] }}</button><button id="proxy-direct" type="button" class="secondary">{{ $t['use_direct'] }}</button></div><div id="proxies" class="list"></div></section>
        <section><h2>{{ $t['accounts'] }}</h2><form id="account-add" class="inline"><input name="label" maxlength="100" placeholder="{{ $t['account_name'] }}" required><button>{{ $t['add_account'] }}</button></form><div id="accounts" class="list"></div><div id="authorization-panel" hidden></div></section>
        <section><h2>{{ $t['quick_replies'] }}</h2><form id="reply-add" class="inline"><input name="text" maxlength="512" placeholder="{{ $t['reply_text'] }}" required><button>{{ $t['add'] }}</button></form><div id="replies" class="list"></div></section>
        <section><h2>{{ $t['connected_pebbles'] }}</h2><p>{{ $t['devices_intro'] }}</p><form id="device-add" class="inline"><input name="name" maxlength="100" value="Pebble" placeholder="{{ $t['device_name'] }}" required><button>{{ $t['create_device_token'] }}</button></form><output id="device-token" hidden></output><div id="devices" class="list"></div></section>
        <section><h2>{{ $t['access'] }}</h2><p>{{ $t['recovery_intro'] }}</p><button id="rotate-recovery" class="secondary">{{ $t['new_recovery_code'] }}</button> <button id="sign-out" class="secondary">{{ $t['sign_out'] }}</button><output id="recovery" hidden></output></section>
    </div>
</main>
<script id="strings" type="application/json">@json($t)</script>
<script src="/assets/settings-flow.js?v=3" defer></script>
<script src="/assets/settings-api.js?v=3" defer></script>
<script src="/assets/settings-qr.js?v=1" defer></script>
<script src="/assets/settings.js?v=18" defer></script>
</body>
</html>
