(() => {
  'use strict';
  const status = document.getElementById('status');
  const flow = globalThis.TeleBezelSettingsFlow;
  const client = globalThis.TeleBezelSettingsApi.create({
    fetch: globalThis.fetch.bind(globalThis),
    csrf: document.querySelector('meta[name="csrf-token"]').content,
    flow,
    onUnauthorized: () => {
      document.getElementById('configuration').hidden = true;
      document.getElementById('access').hidden = false;
    },
    onSessionExpired: () => {
      status.textContent = 'This page has expired. Reload it to continue.';
    }
  });
  const request = client.request;
  const recordOwnerActivity = client.recordOwnerActivity;
  const node = (tag, text, className) => { const element = document.createElement(tag); if (text != null) element.textContent = text; if (className) element.className = className; return element; };
  const formValue = (form, name) => new FormData(form).get(name)?.toString() || '';
  let revision = null;
  let accountRefreshTimer = null;
  let accountRefreshDelay = 1000;

  const renderSettings = async () => {
    const [settings, accountData, replies, devices, proxies] = await Promise.all([
      request('/v1/owner/settings'), request('/v1/owner/telegram/accounts'), request('/v1/owner/quick-replies'), request('/v1/owner/devices'), request('/v1/owner/proxies')
    ]);
    revision = settings.configuration_revision;
    document.getElementById('configuration').hidden = false;
    document.getElementById('access').hidden = true;
    document.querySelector('#telegram [name="telegram_api_id"]').value = settings.telegram.api_id || '';
    document.querySelector('#proxy-policy [name="failure_action"]').value = settings.proxy_runtime.failure_action;
    document.querySelector('#proxy-policy [name="connect_timeout_seconds"]').value = settings.proxy_runtime.connect_timeout_seconds;
    const dl = document.getElementById('runtime-status'); dl.replaceChildren();
    const values = {Instance: settings.instance_id, 'Configuration revision': revision, Scheduler: settings.scheduler?.last_result || 'not observed', 'Last scheduler tick': settings.scheduler?.last_tick_at || 'never', 'Blocked accounts': settings.scheduler?.blocked_accounts ? `${settings.scheduler.blocked_accounts} (run telebezel:accounts-unblock after fixing the cause)` : 0};
    Object.entries(values).forEach(([key, value]) => dl.append(node('dt', key), node('dd', String(value))));
    renderAccounts(accountData); renderReplies(replies); renderDevices(devices); renderProxies(proxies, settings.proxy_runtime.active_profile_id);
  };

  const showRecovery = code => { const output = document.getElementById('recovery'); output.hidden = false; output.textContent = `Save this recovery code now:\n${code}`; };
  const authenticated = async authData => {
    client.authenticated();
    if (authData?.recovery_code) showRecovery(authData.recovery_code);
    await renderSettings(); status.textContent = 'Signed in. Changes are saved on this server.';
  };
  document.getElementById('login').addEventListener('submit', async event => { event.preventDefault(); try { await authenticated(await request('/v1/owner/login', {method: 'POST', body: JSON.stringify({password: formValue(event.target, 'password')})})); } catch (error) { status.textContent = error.message; } });
  document.getElementById('bootstrap').addEventListener('submit', async event => { event.preventDefault(); try { await authenticated(await request('/v1/owner/bootstrap', {method: 'POST', body: JSON.stringify({bootstrap_code: formValue(event.target, 'bootstrap_code'), password: formValue(event.target, 'password')})})); } catch (error) { status.textContent = error.message; } });
  document.getElementById('recover').addEventListener('submit', async event => { event.preventDefault(); try { await authenticated(await request('/v1/owner/recover', {method: 'POST', body: JSON.stringify({recovery_code: formValue(event.target, 'recovery_code'), password: formValue(event.target, 'password')})})); } catch (error) { status.textContent = error.message; } });

  document.getElementById('telegram').addEventListener('submit', async event => { event.preventDefault(); const hash = formValue(event.target, 'telegram_api_hash'); const body = {configuration_revision: revision, telegram_api_id: Number(formValue(event.target, 'telegram_api_id'))}; if (hash) body.telegram_api_hash = hash; try { revision = (await request('/v1/owner/settings', {method: 'PUT', body: JSON.stringify(body)})).configuration_revision; event.target.reset(); await renderSettings(); status.textContent = 'Telegram credentials saved and reconciliation requested.'; } catch (error) { status.textContent = error.message; } });
  const renderProxies = (proxies, activeId) => {
    const list = document.getElementById('proxies'); list.replaceChildren();
    let current = document.getElementById('proxy-current');
    if (!current) { current = node('p'); current.id = 'proxy-current'; list.closest('section').querySelector('h2').after(current); }
    const activeProfile = proxies.find(proxy => proxy.id === activeId);
    current.textContent = activeProfile ? `Current: ${activeProfile.label} · ${activeProfile.mode} · ${activeProfile.host}:${activeProfile.port}` : 'Current: direct connection';
    proxies.forEach(proxy => {
      const item = node('div', null, 'item'); const copy = node('div');
      const ping = proxy.ping.ok === true ? `${proxy.ping.latency_ms} ms` : proxy.ping.ok === false ? `unreachable · ${proxy.ping.error}` : 'not tested';
      copy.append(node('p', `${proxy.label}${proxy.active ? ' · ACTIVE' : ''}`), node('p', `${proxy.mode} · ${proxy.host}:${proxy.port} · ${ping}`, 'meta'));
      const actions = node('div');
      const activate = node('button', proxy.active ? 'Active' : 'Activate', 'secondary'); activate.type = 'button'; activate.disabled = proxy.active; activate.addEventListener('click', async () => { try { await request(`/v1/owner/proxies/${proxy.id}/activate`, {method: 'POST', body: '{}'}); await renderSettings(); } catch (error) { status.textContent = error.message; } });
      const pingButton = node('button', 'Ping', 'secondary'); pingButton.type = 'button'; pingButton.addEventListener('click', async () => { try { await request(`/v1/owner/proxies/${proxy.id}/ping`, {method: 'POST', body: '{}'}); await renderSettings(); } catch (error) { status.textContent = error.message; } });
      const remove = node('button', 'Delete', 'danger'); remove.type = 'button'; remove.disabled = proxy.active; remove.addEventListener('click', async () => { try { await request(`/v1/owner/proxies/${proxy.id}`, {method: 'DELETE'}); await renderSettings(); } catch (error) { status.textContent = error.message; } });
      actions.append(activate, pingButton, remove); item.append(copy, actions); list.append(item);
    });
  };
  document.getElementById('proxy-add').addEventListener('submit', async event => { event.preventDefault(); const mode = formValue(event.target, 'mode'); const body = {label: formValue(event.target, 'label'), mode, host: formValue(event.target, 'host'), port: Number(formValue(event.target, 'port'))}; const username = formValue(event.target, 'username'); const credential = formValue(event.target, 'credential'); if (username) body.username = username; if (credential) body[mode === 'mtproto' ? 'secret' : 'password'] = credential; if (mode === 'http') body.http_only = event.target.elements.http_only.checked; try { await request('/v1/owner/proxies', {method: 'POST', body: JSON.stringify(body)}); event.target.reset(); await renderSettings(); status.textContent = 'Proxy added and tested.'; } catch (error) { status.textContent = error.message; } });
  document.getElementById('proxy-policy').addEventListener('submit', async event => { event.preventDefault(); try { await request('/v1/owner/proxies/settings', {method: 'PUT', body: JSON.stringify({failure_action: formValue(event.target, 'failure_action'), connect_timeout_seconds: Number(formValue(event.target, 'connect_timeout_seconds'))})}); await renderSettings(); status.textContent = 'Proxy failover policy saved.'; } catch (error) { status.textContent = error.message; } });
  document.getElementById('proxy-ping-all').addEventListener('click', async () => { try { status.textContent = 'Testing proxies…'; await request('/v1/owner/proxies/ping', {method: 'POST', body: '{}'}); await renderSettings(); status.textContent = 'Proxy test completed.'; } catch (error) { status.textContent = error.message; } });
  document.getElementById('proxy-direct').addEventListener('click', async () => { try { await request('/v1/owner/proxies/direct', {method: 'POST', body: '{}'}); await renderSettings(); status.textContent = 'Direct connection selected.'; } catch (error) { status.textContent = error.message; } });

  const scheduleAccountRefresh = () => {
    if (accountRefreshTimer !== null || document.hidden) return;
    accountRefreshTimer = setTimeout(async () => {
      accountRefreshTimer = null;
      try {
        renderAccounts(await request('/v1/owner/telegram/accounts'));
        accountRefreshDelay = Math.min(Math.round(accountRefreshDelay * 1.5), 5000);
      } catch (error) {
        if (error.status === 401) status.textContent = 'Your owner session expired. Reload this page to sign in again.';
        else {
          status.textContent = error.message;
          scheduleAccountRefresh();
        }
      }
    }, accountRefreshDelay);
  };
  const renderAccounts = accounts => {
    const list = document.getElementById('accounts');
    let preparing = false;
    list.replaceChildren();
    accounts.forEach(account => {
      const view = flow.accountView(account);
      preparing ||= view.shouldPoll;
      const item = node('div', null, 'item');
      const copy = node('div');
      const details = `${account.lifecycle} · auth ${view.authorizationState} · connection ${view.connectionState}${view.errorCode ? ` · error ${view.errorCode}` : ''}`;
      copy.append(node('p', account.label || 'Telegram account'), node('p', details, 'meta'));
      const auth = node('button', view.buttonLabel, 'secondary');
      auth.type = 'button';
      auth.disabled = !view.canAuthorize;
      auth.setAttribute('aria-busy', view.waitingForRuntime ? 'true' : 'false');
      auth.addEventListener('click', () => authorize(account.id));
      const logout = node('button', 'Log out', 'danger');
      logout.type = 'button';
      logout.disabled = account.lifecycle !== 'active';
      logout.addEventListener('click', async () => { try { await request(`/v1/owner/telegram/accounts/${account.id}/logout`, {method: 'POST', body: '{}'}); await renderSettings(); } catch (error) { status.textContent = error.message; } });
      const actions = node('div');
      actions.append(auth, logout);
      item.append(copy, actions);
      list.append(item);
    });
    if (preparing) scheduleAccountRefresh();
    else accountRefreshDelay = 1000;
  };
  let authorizationTimer = null;
  let authorizationSequence = 0;
  const qrcode = globalThis.TeleBezelQr;
  const authorizationActions = {
    submit_phone_number: ['Phone number', 'tel', 'Send phone number'],
    submit_code: ['Login code', 'text', 'Send code'],
    submit_password: ['Two-step password', 'password', 'Unlock'],
    submit_email_address: ['Email address', 'email', 'Send email'],
    submit_email_code: ['Email code', 'text', 'Send email code'],
    start_qr: [null, null, 'Use QR login'],
    resend_code: [null, null, 'Resend code']
  };
  const stopAuthorizationPolling = () => {
    if (authorizationTimer !== null) clearTimeout(authorizationTimer);
    authorizationTimer = null;
  };
  const renderQr = (panel, link) => {
    let markup = null;
    try { markup = qrcode.svg(link, 4, 4); } catch { markup = null; }
    if (markup === null) {
      panel.append(node('p', `Open this link on the phone running Telegram: ${link}`, 'meta'));
      return;
    }
    const holder = node('div', null, 'qr');
    holder.innerHTML = markup;
    holder.firstChild.setAttribute('aria-label', 'Telegram login QR code');
    panel.append(holder, node('p', 'In Telegram: Settings → Devices → Link Desktop Device, then scan this code.', 'meta'));
  };
  const renderAuthorization = (id, sequence, auth) => {
    if (sequence !== authorizationSequence) return;
    stopAuthorizationPolling();
    const panel = document.getElementById('authorization-panel');
    panel.hidden = false;
    panel.replaceChildren(node('h3', `Authorization · ${auth.state || 'unknown'}`));
    if (auth.state === 'ready') {
      panel.append(node('p', 'This account is authorized.', 'meta'));
      return;
    }
    if (auth.qr_link) renderQr(panel, auth.qr_link);
    (auth.allowed_actions || []).forEach(action => {
      const definition = authorizationActions[action];
      if (!definition) return;
      const form = node('form', null, 'inline');
      let input = null;
      if (definition[0]) {
        const label = node('label', definition[0]);
        input = node('input'); input.type = definition[1]; input.required = true;
        label.append(input); form.append(label);
      }
      const button = node('button', definition[2]); form.append(button);
      form.addEventListener('submit', async event => {
        event.preventDefault();
        if (sequence !== authorizationSequence) return;
        try {
          await request(`/v1/owner/telegram/accounts/${id}/authorization/actions`, {
            method: 'POST', body: JSON.stringify(flow.authorizationPayload(auth, action, input ? input.value : null))
          });
          await renderSettings();
          await authorize(id);
        } catch (error) { status.textContent = error.message; }
      });
      panel.append(form);
    });
    if (auth.state === 'awaiting_qr_confirmation') {
      authorizationTimer = setTimeout(() => {
        if (sequence === authorizationSequence) authorize(id);
      }, 3000);
    }
  };
  const authorize = async id => {
    stopAuthorizationPolling();
    authorizationSequence += 1;
    const sequence = authorizationSequence;
    try { renderAuthorization(id, sequence, await request(`/v1/owner/telegram/accounts/${id}/authorization`)); }
    catch (error) { if (sequence === authorizationSequence) status.textContent = error.message; }
  };
  const pendingAccountKey = 'telebezel.pending-account-create';
  document.getElementById('account-add').addEventListener('submit', async event => {
    event.preventDefault();
    const body = JSON.stringify({label: formValue(event.target, 'label'), proxy: {mode: 'inherit'}});
    let pending;
    try { pending = JSON.parse(sessionStorage.getItem(pendingAccountKey) || 'null'); } catch { pending = null; }
    if (!pending || pending.body !== body) {
      pending = {body, key: crypto.randomUUID()};
      sessionStorage.setItem(pendingAccountKey, JSON.stringify(pending));
    }
    try {
      await request('/v1/owner/telegram/accounts', {method: 'POST', headers: {'Idempotency-Key': pending.key}, body});
      sessionStorage.removeItem(pendingAccountKey);
      event.target.reset();
      await renderSettings();
    } catch (error) { status.textContent = error.message; }
  });

  const renderReplies = replies => {
    const list = document.getElementById('replies'); list.replaceChildren();
    replies.forEach((reply, index) => {
      const item = node('div', null, 'item'); item.append(node('p', reply.text));
      const edit = node('button', 'Edit', 'secondary'); edit.type = 'button';
      edit.addEventListener('click', () => {
        const form = node('form', null, 'inline'); const input = node('input');
        input.value = reply.text; input.maxLength = 512; input.required = true;
        form.append(input, node('button', 'Save'));
        form.addEventListener('submit', async event => {
          event.preventDefault();
          try { await request(`/v1/owner/quick-replies/${reply.id}`, {method: 'PUT', body: JSON.stringify({text: input.value})}); await renderSettings(); }
          catch (error) { status.textContent = error.message; }
        });
        item.replaceChildren(form);
      });
      item.append(edit);
      for (const [label, offset] of [['Up', -1], ['Down', 1]]) {
        const move = node('button', label, 'secondary'); move.type = 'button';
        move.disabled = index + offset < 0 || index + offset >= replies.length;
        move.addEventListener('click', async () => {
          const ids = replies.map(value => value.id);
          [ids[index], ids[index + offset]] = [ids[index + offset], ids[index]];
          try { await request('/v1/owner/quick-replies/reorder', {method: 'PUT', body: JSON.stringify({ids})}); await renderSettings(); }
          catch (error) { status.textContent = error.message; }
        });
        item.append(move);
      }
      const remove = node('button', 'Delete', 'danger'); remove.type = 'button';
      remove.addEventListener('click', async () => {
        try { await request(`/v1/owner/quick-replies/${reply.id}`, {method: 'DELETE'}); await renderSettings(); }
        catch (error) { status.textContent = error.message; }
      });
      item.append(remove); list.append(item);
    });
  };
  document.getElementById('reply-add').addEventListener('submit', async event => { event.preventDefault(); try { await request('/v1/owner/quick-replies', {method: 'POST', body: JSON.stringify({text: formValue(event.target, 'text')})}); event.target.reset(); await renderSettings(); } catch (error) { status.textContent = error.message; } });
  const renderDevices = devices => { const list = document.getElementById('devices'); list.replaceChildren(); devices.forEach(device => { const item = node('div', null, 'item'); item.append(node('p', device.name), node('p', device.revoked_at ? 'revoked' : `last seen ${device.last_seen_at || 'never'}`, 'meta')); if (!device.revoked_at) { const revoke = node('button', 'Revoke', 'danger'); revoke.type = 'button'; revoke.addEventListener('click', async () => { await request(`/v1/owner/devices/${device.id}`, {method: 'DELETE'}); await renderSettings(); }); item.append(revoke); } list.append(item); }); };
  document.getElementById('device-add').addEventListener('submit', async event => { event.preventDefault(); try { const device = await request('/v1/owner/devices', {method: 'POST', body: JSON.stringify({name: formValue(event.target, 'name')})}); const output = document.getElementById('device-token'); output.hidden = false; output.textContent = `Copy this token now; it will not be shown again:\n${device.token}`; await renderSettings(); status.textContent = 'Device token created. Paste it into the TeleBezel Clay settings.'; } catch (error) { status.textContent = error.message; } });
  document.getElementById('rotate-recovery').addEventListener('click', async () => { try { const data = await request('/v1/owner/recovery-code', {method: 'POST', body: '{}'}); showRecovery(data.recovery_code); status.textContent = 'New recovery code created. Save it now.'; } catch (error) { status.textContent = error.message; } });
  document.getElementById('sign-out').addEventListener('click', async () => { await request('/v1/owner/logout', {method: 'POST', body: '{}'}); location.reload(); });

  authenticated().catch(error => {
    if (error.status === 401) {
      document.getElementById('access').hidden = false;
      status.textContent = 'Sign in to manage this server.';
    } else {
      document.getElementById('access').hidden = false;
      status.textContent = `${error.message}. Sign in or recover access to retry.`;
    }
  });
})();
