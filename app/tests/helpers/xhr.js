'use strict';
var http = require('http');
var urlModule = require('url');

function XMLHttpRequest() {
  this.readyState = 0;
  this.status = 0;
  this.responseText = '';
  this.timeout = 0;
  this._headers = {};
  this._responseHeaders = {};
}
XMLHttpRequest.prototype.open = function(method, url) { this._method = method; this._url = url; this.readyState = 1; };
XMLHttpRequest.prototype.setRequestHeader = function(name, value) { this._headers[name] = value; };
XMLHttpRequest.prototype.getResponseHeader = function(name) {
  var value = this._responseHeaders[String(name).toLowerCase()];
  return value === undefined ? null : value;
};
XMLHttpRequest.prototype.abort = function() {
  this._aborted = true;
  if (this._request) { this._request.destroy(); }
};
XMLHttpRequest.prototype.send = function(body) {
  var self = this;
  var parsed = urlModule.parse(this._url);
  var request = http.request({hostname: parsed.hostname, port: parsed.port, path: parsed.path, method: this._method, headers: this._headers}, function(response) {
    var chunks = [];
    response.on('data', function(chunk) { chunks.push(chunk); });
    response.on('end', function() {
      if (self._aborted) { return; }
      self.status = response.statusCode;
      self._responseHeaders = response.headers;
      self.responseText = Buffer.concat(chunks).toString('utf8');
      self.readyState = 4;
      if (self.onreadystatechange) { self.onreadystatechange(); }
    });
  });
  this._request = request;
  if (this.timeout) { request.setTimeout(this.timeout, function() { request.destroy(); if (!self._aborted && self.ontimeout) { self.ontimeout(); } }); }
  request.on('error', function() { if (!self._aborted && self.onerror) { self.onerror(); } });
  if (body !== null && body !== undefined) { request.write(body); }
  request.end();
};

module.exports = XMLHttpRequest;
