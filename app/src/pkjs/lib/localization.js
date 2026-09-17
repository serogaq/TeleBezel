'use strict';
function resolve(locales, locale) { var code = String(locale || 'en').toLowerCase().slice(0, 2); return locales[code] || locales.en; }
module.exports = {resolve: resolve};
