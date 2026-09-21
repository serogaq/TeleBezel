'use strict';
function build(s) {
  return [
    {type: 'heading', defaultValue: s.title},
    {type: 'input', id: 'address', messageKey: 'CONFIG_ADDRESS', label: s.address, description: s.address_hint, attributes: {placeholder: s.address_placeholder, autocapitalize: 'none', autocorrect: 'off', required: true}},
    {type: 'toggle', id: 'ssl', messageKey: 'CONFIG_SSL', label: s.ssl, description: s.ssl_hint, defaultValue: true},
    {type: 'input', id: 'token', messageKey: 'CONFIG_TOKEN', label: s.token, description: s.token_hint, attributes: {type: 'password', placeholder: s.token_placeholder, autocapitalize: 'none', autocorrect: 'off'}},
    {type: 'submit', defaultValue: s.save}
  ];
}
module.exports = {build: build};
