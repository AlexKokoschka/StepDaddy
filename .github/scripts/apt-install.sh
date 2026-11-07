#!/bin/bash
# Installs Ubuntu packages on a GitHub runner without hanging the job.
#   .github/scripts/apt-install.sh pkg...
#
# The firmware manager job hung here for 10+ minutes again and again in Oct
# 2026, while the Yummy job's identical install ran in a minute. The usual
# cause on a fresh runner is Ubuntu's automatic updates holding the package
# lock: apt then waits for it, indefinitely. So those are stopped first, apt
# waits for the lock for at most 2 minutes, every network fetch has a timeout,
# and each attempt is cut off after 5 minutes and retried. Each step's time is
# reported as a notice, which shows on the run's summary (and in the API),
# so a hang that still happens says where.
set -u
note() { echo "::notice title=apt::$*"; }

busy=$(pgrep -a 'apt|dpkg|unattended' | grep -v apt-install | tr '\n' ';' || true)
[ -n "$busy" ] && note "busy before install: $busy"
sudo timeout 60 systemctl stop unattended-upgrades.service apt-daily.service apt-daily-upgrade.service \
  apt-daily.timer apt-daily-upgrade.timer 2>/dev/null || true
sudo pkill -f unattended-upgrade 2>/dev/null || true

# man-db's index rebuild after an install is slow on runners: off.
echo "set man-db/auto-update false" | sudo timeout 20 debconf-communicate >/dev/null 2>&1 || true
sudo rm -f /var/lib/man-db/auto-update

apt_get() {
  sudo DEBIAN_FRONTEND=noninteractive timeout -k 10 300 apt-get -q \
    -o DPkg::Lock::Timeout=120 -o Acquire::Retries=3 \
    -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30 "$@"
}

for attempt in 1 2 3; do
  t0=$(date +%s)
  if apt_get update; then
    t1=$(date +%s)
    if apt_get install -y "$@"; then
      t2=$(date +%s)
      note "attempt $attempt: update $((t1 - t0)) s, install $((t2 - t1)) s"
      exit 0
    else
      note "attempt $attempt: install failed or timed out after $(($(date +%s) - t1)) s (update took $((t1 - t0)) s)"
    fi
  else
    note "attempt $attempt: update failed or timed out after $(($(date +%s) - t0)) s"
  fi
  busy=$(pgrep -a 'apt|dpkg|unattended' | tr '\n' ';' || true)
  [ -n "$busy" ] && note "still running: $busy"
  sudo timeout 60 dpkg --configure -a || true
done
echo "::error title=apt::could not install $* after 3 attempts"
exit 1
