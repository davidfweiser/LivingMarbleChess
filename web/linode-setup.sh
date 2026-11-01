#!/usr/bin/env bash
# Living Marble - sets up a server (a Linode, or any Ubuntu 24.04 machine) to serve the
# game's web page: builds it from GitHub with Emscripten, serves it with nginx over HTTPS
# (a free Let's Encrypt certificate) with the headers the game's threads need, and runs
# the relay for online games (web/relay.py) as a service behind nginx at /ws.
#
#   sudo bash linode-setup.sh chess.example.com you@example.com
#
# The name must already point at this server (a DNS "A" record). Run the script again
# at any time to update the page to the latest version on GitHub.
#   LM_BRANCH=some-branch   build another branch (default: main)
#   LM_SKIP_HTTPS=1         no certificate (testing only: browsers start the game's
#                           threads on HTTPS pages only, so plain http:// won't play)
set -euo pipefail

DOMAIN="${1:-}"
EMAIL="${2:-}"
REPO="${LM_REPO:-https://github.com/davidfweiser/LivingMarbleChess.git}"
BRANCH="${LM_BRANCH:-main}"
SRC=/opt/living-marble/src
WEBROOT=/var/www/living-marble

say() { printf '\n\033[1;33m==> %s\033[0m\n' "$*"; }
die() { printf '\n\033[1;31mError: %s\033[0m\n' "$*" >&2; exit 1; }

[ "$(id -u)" = 0 ] || die "run it as root: sudo bash $0 $*"
[ -n "$DOMAIN" ] || die "usage: sudo bash $0 your.domain.name you@example.com"
[ -n "$EMAIL" ] || [ -n "${LM_SKIP_HTTPS:-}" ] || die "give an email address for the certificate (Let's Encrypt writes before it expires)"
. /etc/os-release
[ "${ID:-}" = ubuntu ] || echo "Warning: written for Ubuntu 24.04; this is ${PRETTY_NAME:-unknown}."

# The name has to lead here, or Let's Encrypt can't check it
if [ -z "${LM_SKIP_HTTPS:-}" ]; then
  addrs="$(getent ahosts "$DOMAIN" | awk '{print $1}' | sort -u || true)"
  [ -n "$addrs" ] || die "$DOMAIN doesn't resolve yet. Add an A record pointing at this server's IP address, wait a few minutes, and run this again."
  mine="$(hostname -I)"
  found=""
  for a in $addrs; do
    case " $mine " in *" $a "*) found=1 ;; esac
  done
  [ -n "$found" ] || die "$DOMAIN points at ${addrs//$'\n'/ }, not at this server ($mine). Fix the DNS record and run this again."
fi

# Building needs about 2 GB of memory: add swap on smaller plans
if [ "$(awk '/MemTotal/{print int($2/1024)}' /proc/meminfo)" -lt 3000 ] && ! swapon --show | grep -q .; then
  say "Adding 2 GB of swap for the build"
  fallocate -l 2G /swapfile && chmod 600 /swapfile && mkswap /swapfile >/dev/null && swapon /swapfile
  grep -q '^/swapfile' /etc/fstab || echo '/swapfile none swap sw 0 0' >> /etc/fstab
fi

say "Installing nginx, certbot, the Emscripten compiler and the relay's websockets"
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q nginx certbot python3-certbot-nginx git cmake make emscripten python3-websockets

say "Getting the game ($BRANCH)"
if [ -d "$SRC/.git" ]; then
  git -C "$SRC" fetch -q --depth 1 origin "$BRANCH"
  git -C "$SRC" checkout -q -B "$BRANCH" FETCH_HEAD
else
  mkdir -p "$(dirname "$SRC")"
  git clone -q --depth 1 --branch "$BRANCH" "$REPO" "$SRC"
fi
[ -f "$SRC/web/index.html" ] || die "branch $BRANCH has no web version (web/index.html); set LM_BRANCH"

say "Building the web page (a few minutes)"
emcmake cmake -S "$SRC" -B "$SRC/build-web" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$SRC/build-web" -j"$(nproc)"

say "Publishing it to $WEBROOT"
stage="$(mktemp -d /var/www/.living-marble.XXXXXX)"
for f in index.html living_marble.js living_marble.wasm living_marble.worker.js living_marble.data \
         LICENSE.txt THIRD_PARTY_NOTICES.txt; do
  cp "$SRC/build-web/$f" "$stage/"
  gzip -9 -k "$stage/$f"  # nginx sends these to browsers that take gzip
done
chmod 755 "$stage" && chmod 644 "$stage"/*
if [ -d "$WEBROOT" ]; then mv "$WEBROOT" "$WEBROOT.old"; fi
mv "$stage" "$WEBROOT"
rm -rf "$WEBROOT.old"

say "Starting the relay for online games"
cat > /etc/systemd/system/living-marble-relay.service <<'EOF'
# Living Marble - the relay for online games (written by web/linode-setup.sh)
[Unit]
Description=Living Marble relay for online games
After=network.target

[Service]
ExecStart=/usr/bin/python3 /opt/living-marble/relay.py --host 127.0.0.1 --port 8765
DynamicUser=yes
Restart=always
RestartSec=2
NoNewPrivileges=yes
PrivateTmp=yes

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
# (the games in progress live in the relay's memory: it restarts only when it has changed)
if ! cmp -s "$SRC/web/relay.py" /opt/living-marble/relay.py; then
  install -m 644 "$SRC/web/relay.py" /opt/living-marble/relay.py
  systemctl enable living-marble-relay >/dev/null 2>&1 || true
  systemctl restart living-marble-relay
else
  systemctl enable --now living-marble-relay >/dev/null 2>&1 || true
fi

say "Configuring nginx for $DOMAIN"
listen6=""
if [ -s /proc/net/if_inet6 ]; then listen6="listen [::]:80;"; fi
cat > /etc/nginx/sites-available/living-marble <<EOF
# Living Marble's web page (written by web/linode-setup.sh; certbot adds the HTTPS lines)
server {
    listen 80;
    $listen6
    server_name $DOMAIN;
    root $WEBROOT;
    index index.html;

    # The game runs on several threads, which browsers allow only on a cross-origin isolated page
    add_header Cross-Origin-Opener-Policy "same-origin" always;
    add_header Cross-Origin-Embedder-Policy "require-corp" always;
    add_header Cross-Origin-Resource-Policy "same-origin" always;
    add_header X-Content-Type-Options "nosniff" always;
    # The files keep their names from version to version: browsers check for a newer one each visit
    add_header Cache-Control "no-cache" always;

    gzip_static on;
    location ~ \.wasm\$ { default_type application/wasm; }
    location / { try_files \$uri \$uri/ =404; }

    # Online games: the relay (web/relay.py), over a WebSocket
    location = /ws {
        proxy_pass http://127.0.0.1:8765;
        proxy_http_version 1.1;
        proxy_set_header Upgrade \$http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_read_timeout 1h;
        proxy_send_timeout 1h;
    }
}
EOF
ln -sf /etc/nginx/sites-available/living-marble /etc/nginx/sites-enabled/living-marble
rm -f /etc/nginx/sites-enabled/default  # (nginx's welcome page: this server is for the game)
nginx -t
systemctl reload nginx || systemctl restart nginx

if command -v ufw >/dev/null && ufw status | grep -q 'Status: active'; then
  ufw allow 'Nginx Full' >/dev/null
fi

if [ -z "${LM_SKIP_HTTPS:-}" ]; then
  say "Getting the HTTPS certificate"
  certbot --nginx --non-interactive --agree-tos -m "$EMAIL" -d "$DOMAIN" --redirect --keep-until-expiring
  systemctl reload nginx
  url="https://$DOMAIN/"
else
  url="http://$DOMAIN/  (no HTTPS: the game won't start its threads there)"
fi

say "Done: $url"
echo "Run this script again to update the page. The certificate renews itself."
echo "The relay's log: journalctl -u living-marble-relay"
