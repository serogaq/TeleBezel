(() => {
  'use strict';
  const status = document.getElementById('status');
  const flow = globalThis.TeleBezelSettingsFlow;
  const strings = JSON.parse(document.getElementById('strings').textContent);
  const t = (key, values = {}) => (strings[key] ?? key).replace(/\{(\w+)\}/g, (_, name) => String(values[name] ?? ''));
  const explain = error => strings[`error.${error.message}`] ?? error.message;
  document.getElementById('language').addEventListener('change', event => {
    document.cookie = `telebezel_locale=${event.target.value}; path=/; max-age=31536000; SameSite=Strict`;
    location.reload();
  });
  let leaving = false;
  const client = globalThis.TeleBezelSettingsApi.create({
    fetch: globalThis.fetch.bind(globalThis),
    csrf: document.querySelector('meta[name="csrf-token"]').content,
    apiCsrf: document.querySelector('meta[name="api-csrf"]').content,
    flow,
    onUnauthorized: () => {
      if (leaving) return;
      document.getElementById('configuration').hidden = true;
      document.getElementById('access').hidden = false;
    },
    onSessionExpired: () => {
      status.textContent = t('page_expired');
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
      request('/v1/settings'), request('/v1/telegram/accounts'), request('/v1/quick-replies').then(data => data.items), request('/v1/devices'), request('/v1/proxies')
    ]);
    revision = settings.configuration_revision;
    document.getElementById('configuration').hidden = false;
    document.getElementById('access').hidden = true;
    document.querySelector('#telegram [name="telegram_api_id"]').value = settings.telegram.api_id || '';
    document.querySelector('#proxy-policy [name="failure_action"]').value = settings.proxy_runtime.failure_action;
    document.querySelector('#proxy-policy [name="connect_timeout_seconds"]').value = settings.proxy_runtime.connect_timeout_seconds;
    const dl = document.getElementById('runtime-status'); dl.replaceChildren();
    const values = {[t('instance')]: settings.instance_id, [t('configuration_revision')]: revision, [t('scheduler')]: settings.scheduler?.last_result || t('not_observed'), [t('last_tick')]: settings.scheduler?.last_tick_at || t('never'), [t('blocked_accounts')]: settings.scheduler?.blocked_accounts ? t('blocked_hint', {count: settings.scheduler.blocked_accounts}) : 0};
    Object.entries(values).forEach(([key, value]) => dl.append(node('dt', key), node('dd', String(value))));
    renderAccounts(accountData); renderReplies(replies); renderDevices(devices); renderProxies(proxies, settings.proxy_runtime.active_profile_id);
  };

  const showRecovery = code => { const output = document.getElementById('recovery'); output.hidden = false; output.textContent = t('save_recovery', {code}); };
  const authenticated = async authData => {
    client.authenticated(authData);
    if (authData?.recovery_code) showRecovery(authData.recovery_code);
    await renderSettings(); status.textContent = t('signed_in');
  };
  document.getElementById('login').addEventListener('submit', async event => { event.preventDefault(); try { await authenticated(await request('/v1/session/login', {method: 'POST', body: JSON.stringify({password: formValue(event.target, 'password')})})); } catch (error) { status.textContent = explain(error); } });
  document.getElementById('bootstrap').addEventListener('submit', async event => { event.preventDefault(); try { await authenticated(await request('/v1/session/bootstrap', {method: 'POST', body: JSON.stringify({bootstrap_code: formValue(event.target, 'bootstrap_code'), password: formValue(event.target, 'password')})})); } catch (error) { status.textContent = explain(error); } });
  document.getElementById('recover').addEventListener('submit', async event => { event.preventDefault(); try { await authenticated(await request('/v1/session/recover', {method: 'POST', body: JSON.stringify({recovery_code: formValue(event.target, 'recovery_code'), password: formValue(event.target, 'password')})})); } catch (error) { status.textContent = explain(error); } });

  document.getElementById('telegram').addEventListener('submit', async event => { event.preventDefault(); const hash = formValue(event.target, 'telegram_api_hash'); const body = {configuration_revision: revision, telegram_api_id: Number(formValue(event.target, 'telegram_api_id'))}; if (hash) body.telegram_api_hash = hash; try { revision = (await request('/v1/settings', {method: 'PUT', body: JSON.stringify(body)})).configuration_revision; event.target.reset(); await renderSettings(); status.textContent = t('telegram_saved'); } catch (error) { status.textContent = explain(error); } });
  const renderProxies = (proxies, activeId) => {
    const list = document.getElementById('proxies'); list.replaceChildren();
    let current = document.getElementById('proxy-current');
    if (!current) { current = node('p'); current.id = 'proxy-current'; list.closest('section').querySelector('h2').after(current); }
    const activeProfile = proxies.find(proxy => proxy.id === activeId);
    current.textContent = activeProfile ? t('current_proxy', activeProfile) : t('current_direct');
    proxies.forEach(proxy => {
      const item = node('div', null, 'item'); const copy = node('div');
      const ping = proxy.ping.ok === true ? t('latency', {ms: proxy.ping.latency_ms}) : proxy.ping.ok === false ? t('unreachable', {error: proxy.ping.error}) : t('not_tested');
      copy.append(node('p', `${proxy.label}${proxy.active ? t('active_mark') : ''}`), node('p', `${proxy.mode} · ${proxy.host}:${proxy.port} · ${ping}`, 'meta'));
      const actions = node('div');
      const activate = node('button', proxy.active ? t('active') : t('activate'), 'secondary'); activate.type = 'button'; activate.disabled = proxy.active; activate.addEventListener('click', async () => { try { await request(`/v1/proxies/${proxy.id}/activate`, {method: 'POST', body: '{}'}); await renderSettings(); } catch (error) { status.textContent = explain(error); } });
      const pingButton = node('button', t('ping'), 'secondary'); pingButton.type = 'button'; pingButton.addEventListener('click', async () => { try { await request(`/v1/proxies/${proxy.id}/ping`, {method: 'POST', body: '{}'}); await renderSettings(); } catch (error) { status.textContent = explain(error); } });
      const remove = node('button', t('delete'), 'danger'); remove.type = 'button'; remove.disabled = proxy.active; remove.addEventListener('click', async () => { try { await request(`/v1/proxies/${proxy.id}`, {method: 'DELETE'}); await renderSettings(); } catch (error) { status.textContent = explain(error); } });
      actions.append(activate, pingButton, remove); item.append(copy, actions); list.append(item);
    });
  };
  document.getElementById('proxy-add').addEventListener('submit', async event => { event.preventDefault(); const mode = formValue(event.target, 'mode'); const body = {label: formValue(event.target, 'label'), mode, host: formValue(event.target, 'host'), port: Number(formValue(event.target, 'port'))}; const username = formValue(event.target, 'username'); const credential = formValue(event.target, 'credential'); if (username) body.username = username; if (credential) body[mode === 'mtproto' ? 'secret' : 'password'] = credential; if (mode === 'http') body.http_only = event.target.elements.http_only.checked; try { await request('/v1/proxies', {method: 'POST', body: JSON.stringify(body)}); event.target.reset(); await renderSettings(); status.textContent = t('proxy_added'); } catch (error) { status.textContent = explain(error); } });
  document.getElementById('proxy-policy').addEventListener('submit', async event => { event.preventDefault(); try { await request('/v1/proxies/settings', {method: 'PUT', body: JSON.stringify({failure_action: formValue(event.target, 'failure_action'), connect_timeout_seconds: Number(formValue(event.target, 'connect_timeout_seconds'))})}); await renderSettings(); status.textContent = t('policy_saved'); } catch (error) { status.textContent = explain(error); } });
  document.getElementById('proxy-ping-all').addEventListener('click', async () => { try { status.textContent = t('testing_proxies'); await request('/v1/proxies/ping', {method: 'POST', body: '{}'}); await renderSettings(); status.textContent = t('proxy_test_done'); } catch (error) { status.textContent = explain(error); } });
  document.getElementById('proxy-direct').addEventListener('click', async () => { try { await request('/v1/proxies/direct', {method: 'POST', body: '{}'}); await renderSettings(); status.textContent = t('direct_selected'); } catch (error) { status.textContent = explain(error); } });

  const scheduleAccountRefresh = () => {
    if (accountRefreshTimer !== null || document.hidden || leaving) return;
    accountRefreshTimer = setTimeout(async () => {
      accountRefreshTimer = null;
      try {
        renderAccounts(await request('/v1/telegram/accounts'));
        accountRefreshDelay = Math.min(Math.round(accountRefreshDelay * 1.5), 5000);
      } catch (error) {
        if (error.status === 401) status.textContent = t('session_expired');
        else {
          status.textContent = explain(error);
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
      const details = t('account_details', {lifecycle: account.lifecycle, auth: view.authorizationState, connection: view.connectionState}) + (view.errorCode ? t('account_error', {code: view.errorCode}) : '');
      copy.append(node('p', account.label || t('telegram_account')), node('p', details, 'meta'));
      const auth = node('button', t(view.buttonLabel), 'secondary');
      auth.type = 'button';
      auth.disabled = !view.canAuthorize;
      auth.setAttribute('aria-busy', view.waitingForRuntime ? 'true' : 'false');
      auth.addEventListener('click', () => authorize(account.id));
      const logout = node('button', t('log_out'), 'danger');
      logout.type = 'button';
      logout.disabled = account.lifecycle !== 'active';
      logout.addEventListener('click', async () => { try { await request(`/v1/telegram/accounts/${account.id}/logout`, {method: 'POST', body: '{}'}); await renderSettings(); } catch (error) { status.textContent = explain(error); } });
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
    submit_phone_number: ['phone_number', 'tel', 'send_phone'],
    submit_code: ['login_code', 'text', 'send_code'],
    submit_password: ['two_step_password', 'password', 'unlock'],
    submit_email_address: ['email_address', 'email', 'send_email'],
    submit_email_code: ['email_code', 'text', 'send_email_code'],
    start_qr: [null, null, 'use_qr'],
    resend_code: [null, null, 'resend_code']
  };
  const stopAuthorizationPolling = () => {
    if (authorizationTimer !== null) clearTimeout(authorizationTimer);
    authorizationTimer = null;
  };
  const renderQr = (panel, link) => {
    let markup = null;
    try { markup = qrcode.svg(link, 4, 4); } catch { markup = null; }
    if (markup === null) {
      panel.append(node('p', t('qr_link', {link}), 'meta'));
      return;
    }
    const holder = node('div', null, 'qr');
    holder.innerHTML = markup;
    holder.firstChild.setAttribute('aria-label', t('qr_label'));
    panel.append(holder, node('p', t('qr_hint'), 'meta'));
  };
  const renderAuthorization = (id, sequence, auth) => {
    if (sequence !== authorizationSequence) return;
    stopAuthorizationPolling();
    const panel = document.getElementById('authorization-panel');
    panel.hidden = false;
    panel.replaceChildren(node('h3', t('authorization_state', {state: auth.state || t('unknown')})));
    if (auth.state === 'ready') {
      panel.append(node('p', t('account_authorized'), 'meta'));
      return;
    }
    if (auth.qr_link) renderQr(panel, auth.qr_link);
    (auth.allowed_actions || []).forEach(action => {
      const definition = authorizationActions[action];
      if (!definition) return;
      const form = node('form', null, 'inline');
      let input = null;
      if (definition[0]) {
        const label = node('label', t(definition[0]));
        input = node('input'); input.type = definition[1]; input.required = true;
        label.append(input); form.append(label);
      }
      const button = node('button', t(definition[2])); form.append(button);
      form.addEventListener('submit', async event => {
        event.preventDefault();
        if (sequence !== authorizationSequence) return;
        try {
          await request(`/v1/telegram/accounts/${id}/authorization/actions`, {
            method: 'POST', body: JSON.stringify(flow.authorizationPayload(auth, action, input ? input.value : null))
          });
          await renderSettings();
          await authorize(id);
        } catch (error) { status.textContent = explain(error); }
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
    try { renderAuthorization(id, sequence, await request(`/v1/telegram/accounts/${id}/authorization`)); }
    catch (error) { if (sequence === authorizationSequence) status.textContent = explain(error); }
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
      await request('/v1/telegram/accounts', {method: 'POST', headers: {'Idempotency-Key': pending.key}, body});
      sessionStorage.removeItem(pendingAccountKey);
      event.target.reset();
      await renderSettings();
    } catch (error) { status.textContent = explain(error); }
  });

  const renderReplies = replies => {
    const list = document.getElementById('replies'); list.replaceChildren();
    replies.forEach((reply, index) => {
      const item = node('div', null, 'item'); item.append(node('p', reply.text));
      const edit = node('button', t('edit'), 'secondary'); edit.type = 'button';
      edit.addEventListener('click', () => {
        const form = node('form', null, 'inline'); const input = node('input');
        input.value = reply.text; input.maxLength = 512; input.required = true;
        form.append(input, node('button', t('save')));
        form.addEventListener('submit', async event => {
          event.preventDefault();
          try { await request(`/v1/quick-replies/${reply.id}`, {method: 'PUT', body: JSON.stringify({text: input.value})}); await renderSettings(); }
          catch (error) { status.textContent = explain(error); }
        });
        item.replaceChildren(form);
      });
      item.append(edit);
      for (const [label, offset] of [['up', -1], ['down', 1]]) {
        const move = node('button', t(label), 'secondary'); move.type = 'button';
        move.disabled = index + offset < 0 || index + offset >= replies.length;
        move.addEventListener('click', async () => {
          const ids = replies.map(value => value.id);
          [ids[index], ids[index + offset]] = [ids[index + offset], ids[index]];
          try { await request('/v1/quick-replies/reorder', {method: 'PUT', body: JSON.stringify({ids})}); await renderSettings(); }
          catch (error) { status.textContent = explain(error); }
        });
        item.append(move);
      }
      const remove = node('button', t('delete'), 'danger'); remove.type = 'button';
      remove.addEventListener('click', async () => {
        try { await request(`/v1/quick-replies/${reply.id}`, {method: 'DELETE'}); await renderSettings(); }
        catch (error) { status.textContent = explain(error); }
      });
      item.append(remove); list.append(item);
    });
  };
  document.getElementById('reply-add').addEventListener('submit', async event => { event.preventDefault(); try { await request('/v1/quick-replies', {method: 'POST', body: JSON.stringify({text: formValue(event.target, 'text')})}); event.target.reset(); await renderSettings(); } catch (error) { status.textContent = explain(error); } });
  const renderDevices = devices => { const list = document.getElementById('devices'); list.replaceChildren(); devices.forEach(device => { const item = node('div', null, 'item'); item.append(node('p', device.name), node('p', device.revoked_at ? t('revoked') : t('last_seen', {when: device.last_seen_at || t('never')}), 'meta')); if (!device.revoked_at) { const revoke = node('button', t('revoke'), 'danger'); revoke.type = 'button'; revoke.addEventListener('click', async () => { await request(`/v1/devices/${device.id}`, {method: 'DELETE'}); await renderSettings(); }); item.append(revoke); } list.append(item); }); };
  document.getElementById('device-add').addEventListener('submit', async event => { event.preventDefault(); try { const device = await request('/v1/devices', {method: 'POST', body: JSON.stringify({name: formValue(event.target, 'name')})}); const output = document.getElementById('device-token'); output.hidden = false; output.textContent = t('device_token_copy', {token: device.token}); await renderSettings(); status.textContent = t('device_created'); } catch (error) { status.textContent = explain(error); } });
  document.getElementById('rotate-recovery').addEventListener('click', async () => { try { const data = await request('/v1/session/recovery-code', {method: 'POST', body: '{}'}); showRecovery(data.recovery_code); status.textContent = t('recovery_created'); } catch (error) { status.textContent = explain(error); } });
  document.getElementById('sign-out').addEventListener('click', async () => {
    leaving = true;
    clearTimeout(accountRefreshTimer);
    accountRefreshTimer = null;
    try { await request('/v1/session/logout', {method: 'POST', body: '{}'}); } finally { location.reload(); }
  });

  authenticated().catch(error => {
    if (error.status === 401) {
      document.getElementById('access').hidden = false;
      status.textContent = t('sign_in_prompt');
    } else {
      document.getElementById('access').hidden = false;
      status.textContent = t('sign_in_retry', {error: explain(error)});
    }
  });
})();
