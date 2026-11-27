#!/bin/sh
# Deploy the built Vitaleon folder to an Akai Force over SSH.
#
# The plugin folder is replaced as a whole, so a stale file inside it (an old
# skin, an old .so) cannot survive an update. The exceptions are the user data
# next to the plugin: Content/ (banks, presets, samples and wavetables) and the
# runtime files Favourites.json and MidiLearn.json. They are copied into the new
# folder before the old one is moved away; everything else is taken from build/,
# never merged.
#
#   tools/deploy-force.sh              deploy
#   tools/deploy-force.sh --reboot     deploy, then reboot the Force
#   tools/deploy-force.sh --dry-run    show what would be copied, change nothing
#
# Environment: VITALEON_FORCE_HOST (default 192.168.178.23),
#              VITALEON_FORCE_KEY  (default ~/.ssh/akai-force),
#              VITALEON_FORCE_DRIVE (default "/media/FORCE SSD").
set -eu

HOST=${VITALEON_FORCE_HOST:-192.168.178.23}
KEY=${VITALEON_FORCE_KEY:-$HOME/.ssh/akai-force}
DRIVE=${VITALEON_FORCE_DRIVE:-/media/FORCE SSD}
NAME="Andersson Audio - VST - Vitaleon"
SRC="build/$NAME"
PLUGINS="$DRIVE/VST2 Plugins"
STAGE="$DRIVE/.vitaleon-stage"
reboot=no
dry=no
for arg in "$@"; do
    case $arg in
        --reboot) reboot=yes ;;
        --dry-run) dry=yes ;;
        -h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

[ -f "$SRC/plugin.json" ] || { echo "no build at $SRC - run the SDK build first" >&2; exit 1; }
[ -f "$SRC/VST/vitaleon-arm32.so" ] || { echo "no arm32 binary in $SRC/VST" >&2; exit 1; }

SSH="ssh -i $KEY -o IdentitiesOnly=yes -o PreferredAuthentications=publickey -o PasswordAuthentication=no -o StrictHostKeyChecking=accept-new -o BatchMode=yes -o ConnectTimeout=10 root@$HOST"
q() { printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"; }

echo "== target =="
$SSH "grep -m1 '\"version\"' $(q "$PLUGINS/$NAME/plugin.json") 2>/dev/null || echo '(not installed yet)'
echo \"Content files on device: \$(find $(q "$PLUGINS/$NAME/Content") -type f 2>/dev/null | wc -l)\""

if [ "$dry" = yes ]; then
    echo "== would upload =="
    du -sh "$SRC" 2>/dev/null || true
    find "$SRC" -type f | wc -l | sed 's/^/files: /'
    exit 0
fi

echo "== upload =="
tar -cf - -C build "$NAME" | $SSH "rm -rf $(q "$STAGE"); mkdir -p $(q "$STAGE"); tar -xf - -C $(q "$STAGE")"
$SSH "test -f $(q "$STAGE/$NAME/plugin.json")" || { echo "upload incomplete" >&2; exit 1; }

echo "== keep installed Content =="
$SSH "if [ -d $(q "$PLUGINS/$NAME/Content") ]; then cp -a $(q "$PLUGINS/$NAME/Content/.") $(q "$STAGE/$NAME/Content/"); fi
echo \"Content files now: \$(find $(q "$STAGE/$NAME/Content") -type f | wc -l)\""

echo "== keep runtime files =="
$SSH "for f in Favourites.json MidiLearn.json; do
    src=$(q "$PLUGINS/$NAME")/\$f; dst=$(q "$STAGE/$NAME")/\$f
    if [ -f \"\$src\" ]; then cp -a \"\$src\" \"\$dst\"; echo \"kept \$f\"; fi
done"

echo "== swap =="
$SSH "set -e
rm -rf $(q "$PLUGINS/$NAME.old")
mv $(q "$PLUGINS/$NAME") $(q "$PLUGINS/$NAME.old")
mv $(q "$STAGE/$NAME") $(q "$PLUGINS/$NAME")
rm -rf $(q "$STAGE")
sync
echo \"installed: \$(grep -m1 '\"version\"' $(q "$PLUGINS/$NAME/plugin.json"))\"
echo \"arm32: \$(md5sum $(q "$PLUGINS/$NAME/VST/vitaleon-arm32.so") | cut -d' ' -f1)\"
echo \"Content files: \$(find $(q "$PLUGINS/$NAME/Content") -type f | wc -l)\"
echo \"skins: \$(ls $(q "$PLUGINS/$NAME/Plugin Skins") | wc -l)\"
for f in Favourites.json MidiLearn.json; do if [ -f $(q "$PLUGINS/$NAME")/\$f ]; then echo \"runtime: \$f kept\"; fi; done"

echo "== local arm32 =="
md5 -q "$SRC/VST/vitaleon-arm32.so" 2>/dev/null || md5sum "$SRC/VST/vitaleon-arm32.so" | cut -d' ' -f1

if [ "$reboot" = yes ]; then
    echo "== reboot =="
    $SSH "sync; reboot" 2>/dev/null || true
    echo "reboot sent"
fi
