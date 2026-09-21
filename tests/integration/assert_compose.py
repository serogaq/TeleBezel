#!/usr/bin/env python3
import json
import sys

model = json.load(sys.stdin)
services = model['services']
api, tdlib, postgres, scheduler = (services[name] for name in ('backend-api', 'backend-tdlib', 'postgres', 'scheduler'))

def sources(service, key):
    return {entry['source'] for entry in service.get(key, [])}

assert set(api['networks']) == {'api-ingress', 'api-db', 'api-tdlib'}
assert set(tdlib['networks']) == {'api-tdlib', 'tdlib-egress'}
assert set(postgres['networks']) == {'api-db'}
assert set(scheduler['networks']) == {'api-db', 'api-tdlib'}
assert api['ports'][0]['host_ip'] == '127.0.0.1' and api['ports'][0]['target'] == 8080
assert not tdlib.get('ports') and not postgres.get('ports')
assert not api.get('volumes')
assert not scheduler.get('volumes')
tdlib_volumes = tdlib.get('volumes', [])
assert {entry['source'] for entry in tdlib_volumes if entry['type'] == 'volume'} == {'tdlib-data'}
assert all(entry['target'] != '/run/secrets/telebezel-proxies' for entry in tdlib_volumes)
assert sources(postgres, 'volumes') == {'postgres-data'}
assert sources(api, 'secrets') == {'laravel_app_key', 'postgres_password', 'tdlib_internal_token'}
assert sources(tdlib, 'secrets') == {'tdlib_internal_token', 'tdlib_database_master_key'}
assert sources(postgres, 'secrets') == {'postgres_password'}
assert sources(scheduler, 'secrets') == {'laravel_app_key', 'postgres_password', 'tdlib_internal_token'}
assert api['read_only'] and tdlib['read_only'] and postgres['read_only'] and scheduler['read_only']
assert scheduler.get('entrypoint') is None
assert scheduler['command'][:2] == ['sh', '-c']
