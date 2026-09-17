#!/usr/bin/env python3
import json
import sys

model = json.load(sys.stdin)
services = model['services']
api, tdlib, postgres = (services[name] for name in ('backend-api', 'backend-tdlib', 'postgres'))

def sources(service, key):
    return {entry['source'] for entry in service.get(key, [])}

assert set(api['networks']) == {'api-ingress', 'api-db', 'api-tdlib'}
assert set(tdlib['networks']) == {'api-tdlib', 'tdlib-egress'}
assert set(postgres['networks']) == {'api-db'}
assert api['ports'][0]['host_ip'] == '127.0.0.1' and api['ports'][0]['target'] == 8080
assert not tdlib.get('ports') and not postgres.get('ports')
assert not api.get('volumes')
assert sources(tdlib, 'volumes') == {'tdlib-data'}
assert sources(postgres, 'volumes') == {'postgres-data'}
assert sources(api, 'secrets') == {'laravel_app_key', 'postgres_password', 'tdlib_internal_token'}
assert sources(tdlib, 'secrets') == {'tdlib_internal_token'}
assert sources(postgres, 'secrets') == {'postgres_password'}
assert api['read_only'] and tdlib['read_only'] and postgres['read_only']
