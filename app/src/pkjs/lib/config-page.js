'use strict';
function defaultAccount(s, state) {
  if (state.accounts) {
    return [{type: 'select', id: 'default_account', messageKey: 'CONFIG_DEFAULT_ACCOUNT', label: s.default_account, description: s.default_account_hint,
      defaultValue: state.defaultAccount || '',
      options: [{label: s.default_account_none, value: ''}].concat(state.accounts.map(function(account) { return {label: account.name, value: account.id}; }))}];
  }
  if (state.accountsUnavailable) { return [{type: 'text', defaultValue: s.default_account_unavailable}]; }
  return [];
}
function build(s, state) {
  state = state || {};
  var tokenSaved = Boolean(state.tokenSaved);
  return [
    {type: 'heading', defaultValue: s.title},
    {type: 'section', items: [
      {type: 'heading', defaultValue: s.section_connection},
      {type: 'input', id: 'address', messageKey: 'CONFIG_ADDRESS', label: s.address, description: s.address_hint, attributes: {placeholder: s.address_placeholder, autocapitalize: 'none', autocorrect: 'off', required: true}},
      {type: 'toggle', id: 'ssl', messageKey: 'CONFIG_SSL', label: s.ssl, description: s.ssl_hint, defaultValue: true},
      {type: 'input', id: 'token', messageKey: 'CONFIG_TOKEN', label: s.token, description: s.token_hint, attributes: {type: 'password', placeholder: tokenSaved ? s.token_saved_placeholder : s.token_placeholder, autocapitalize: 'none', autocorrect: 'off'}}
    ]},
    {type: 'section', items: [{type: 'heading', defaultValue: s.section_watch}].concat(defaultAccount(s, state), [
      {type: 'toggle', id: 'show_archive', messageKey: 'SHOW_ARCHIVE', label: s.show_archive, description: s.show_archive_hint, defaultValue: true},
      {type: 'select', id: 'unread_mode', messageKey: 'UNREAD_MODE', label: s.unread_mode, description: s.unread_mode_hint, defaultValue: 'chats',
        options: [{label: s.unread_mode_chats, value: 'chats'}, {label: s.unread_mode_messages, value: 'messages'}]}
    ])},
    {type: 'submit', defaultValue: s.save}
  ];
}
module.exports = {build: build};
