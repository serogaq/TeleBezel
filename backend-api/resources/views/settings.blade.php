<!doctype html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <meta name="csrf-token" content="{{ csrf_token() }}">
    <title>TeleBezel Settings</title>
    <link rel="stylesheet" href="/assets/settings.css">
</head>
<body>
<main>
    <header><p class="eyebrow">PEBBLE × TELEGRAM</p><h1>TeleBezel</h1><p id="status" role="status">Connect your Pebble and configure Telegram.</p></header>
    <section id="access" hidden><h2>Owner access</h2><div class="columns">
        <form id="login"><h3>Sign in</h3><label>Password <input name="password" type="password" autocomplete="current-password" required></label><button>Sign in</button></form>
        <form id="bootstrap"><h3>First setup</h3><label>Bootstrap code <input name="bootstrap_code" autocomplete="one-time-code" required></label><label>New password <input name="password" type="password" minlength="12" autocomplete="new-password" required></label><button>Create owner</button></form>
        <form id="recover"><h3>Recover access</h3><label>Recovery code <input name="recovery_code" autocomplete="one-time-code" required></label><label>New password <input name="password" type="password" minlength="12" autocomplete="new-password" required></label><button>Reset access</button></form>
    </div></section>
    <div id="configuration" hidden>
        <section><h2>Status</h2><dl id="runtime-status"></dl></section>
        <section><h2>Telegram application</h2><form id="telegram"><label>API ID <input name="telegram_api_id" inputmode="numeric" required></label><label>API hash <input name="telegram_api_hash" type="password" autocomplete="off" placeholder="Leave blank to keep it"></label><button>Save credentials</button></form></section>
        <section><h2>Proxy profiles</h2><p>The active profile is used by accounts set to inherit. Secrets are never shown again.</p><form id="proxy-add"><div class="columns"><label>Name <input name="label" maxlength="100" required></label><label>Mode <select name="mode"><option value="socks5">SOCKS5</option><option value="http">HTTP</option><option value="mtproto">MTProto</option></select></label></div><div class="columns"><label>Host <input name="host" required></label><label>Port <input name="port" inputmode="numeric" required></label></div><div class="columns"><label>Username <input name="username" autocomplete="off"></label><label>Password / secret <input name="credential" type="password" autocomplete="off"></label></div><label class="check"><input name="http_only" type="checkbox"> HTTP only</label><button>Add and test</button></form><form id="proxy-policy" class="inline"><label>Failure action <select name="failure_action"><option value="next">Switch to next proxy</option><option value="stay">Stay on current proxy</option><option value="direct">Switch to direct</option></select></label><label>Connection timeout, seconds <input name="connect_timeout_seconds" type="number" min="3" max="300" value="10" required></label><button>Save policy</button></form><div class="inline"><button id="proxy-ping-all" type="button" class="secondary">Ping all</button><button id="proxy-direct" type="button" class="secondary">Use direct</button></div><div id="proxies" class="list"></div></section>
        <section><h2>Accounts</h2><form id="account-add" class="inline"><input name="label" maxlength="100" placeholder="Account name" required><button>Add account</button></form><div id="accounts" class="list"></div><div id="authorization-panel" hidden></div></section>
        <section><h2>Quick replies</h2><form id="reply-add" class="inline"><input name="text" maxlength="512" placeholder="Reply text" required><button>Add</button></form><div id="replies" class="list"></div></section>
        <section><h2>Connected Pebbles</h2><p>Create a device token, copy it, then paste it into the TeleBezel Clay settings. The token is shown only once.</p><form id="device-add" class="inline"><input name="name" maxlength="100" value="Pebble" placeholder="Device name" required><button>Create device token</button></form><output id="device-token" hidden></output><div id="devices" class="list"></div></section>
        <section><h2>Access</h2><p>Generating a new recovery code invalidates the previous one.</p><button id="rotate-recovery" class="secondary">Generate new recovery code</button> <button id="sign-out" class="secondary">Sign out</button><output id="recovery" hidden></output></section>
    </div>
</main>
<script src="/assets/settings-flow.js?v=1" defer></script>
<script src="/assets/settings-api.js?v=2" defer></script>
<script src="/assets/settings-qr.js?v=1" defer></script>
<script src="/assets/settings.js?v=15" defer></script>
</body>
</html>
