#!/bin/sh
set -eu

output="${1:-release/lan-ca}"
mkdir -p "$output"
docker compose -f compose.yaml -f compose.lan-https.yaml cp caddy:/data/caddy/pki/authorities/local/root.crt "$output/telebezel-root.crt"
fingerprint=$(openssl x509 -in "$output/telebezel-root.crt" -noout -fingerprint -sha256 | cut -d= -f2 | tr -d ':')
make_uuid() {
  openssl rand -hex 16 | sed 's/^\(........\)\(....\)\(....\)\(....\)\(............\)$/\1-\2-\3-\4-\5/'
}
payload_id="$(make_uuid)"
profile_id="$(make_uuid)"
certificate=$(openssl x509 -in "$output/telebezel-root.crt" -outform DER | openssl base64 -A)
cat > "$output/telebezel-ca.mobileconfig" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>PayloadContent</key><array><dict><key>PayloadCertificateFileName</key><string>telebezel-root.crt</string><key>PayloadContent</key><data>$certificate</data><key>PayloadDescription</key><string>TeleBezel private LAN root CA (SHA-256 $fingerprint)</string><key>PayloadDisplayName</key><string>TeleBezel LAN CA</string><key>PayloadIdentifier</key><string>org.telebezel.ca.$payload_id</string><key>PayloadType</key><string>com.apple.security.root</string><key>PayloadUUID</key><string>$payload_id</string><key>PayloadVersion</key><integer>1</integer></dict></array><key>PayloadDisplayName</key><string>TeleBezel LAN HTTPS</string><key>PayloadIdentifier</key><string>org.telebezel.profile.$profile_id</string><key>PayloadRemovalDisallowed</key><false/><key>PayloadType</key><string>Configuration</string><key>PayloadUUID</key><string>$profile_id</string><key>PayloadVersion</key><integer>1</integer></dict></plist>
EOF
chmod 0644 "$output/telebezel-root.crt" "$output/telebezel-ca.mobileconfig"
echo "Exported public CA files to $output (SHA-256 $fingerprint). No private key was exported."
