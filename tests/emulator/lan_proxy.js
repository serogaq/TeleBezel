'use strict';
var fs = require('fs');
var http = require('http');
var https = require('https');

var target = process.env.TARGET_HOST;
var targetPort = Number(process.env.TARGET_PORT || 443);
var ca = fs.readFileSync(process.env.CA_FILE);
var port = Number(process.env.PORT || 8788);

http.createServer(function(request, response) {
  var headers = Object.assign({}, request.headers, {host: target + (targetPort === 443 ? '' : ':' + targetPort)});
  var upstream = https.request({host: target, port: targetPort, method: request.method, path: request.url, headers: headers, ca: ca}, function(reply) {
    response.writeHead(reply.statusCode, reply.headers);
    reply.pipe(response);
  });
  upstream.on('error', function(error) {
    process.stderr.write('proxy error: ' + error.code + '\n');
    if (!response.headersSent) { response.writeHead(502, {'Content-Type': 'application/json'}); }
    response.end(JSON.stringify({error: {code: 'service.tdlib_unavailable'}}));
  });
  request.pipe(upstream);
}).listen(port, '127.0.0.1', function() {
  process.stdout.write('lan proxy on 127.0.0.1:' + port + ' -> https://' + target + ':' + targetPort + '\n');
});
