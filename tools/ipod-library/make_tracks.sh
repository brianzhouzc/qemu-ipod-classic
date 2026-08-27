#!/usr/bin/env bash
# Emit the TSV mklib reads on stdin, one line per track.
#
# ffprobe's -show_entries output order is not stable across a mixed
# format/stream query, so each value is asked for on its own; a missing
# duration is what produced N/A before.
set -u
SRC=${SRC:-'/mnt/e/AI/Ipod/musica_descargada/Michael Jackson - Videos'}
ALBUM=${ALBUM:-'Greatest Hits'}
ARTIST=${ARTIST:-'Michael Jackson'}

probe() {  # probe <file> <entry>
  ffprobe -v quiet -select_streams a:0 -show_entries "$2" \
          -of default=nw=1:nk=1 "$1" 2>/dev/null | head -1
}

for t in "$@"; do
  f="$SRC/$ARTIST - $t.mp3"
  if [ ! -f "$f" ]; then
    echo "missing: $t" >&2
    continue
  fi
  dur=$(probe "$f" format=duration)
  br=$(probe "$f" stream=bit_rate)
  rate=$(probe "$f" stream=sample_rate)
  case "$dur" in ''|N/A) dur=0 ;; esac
  case "$br"  in ''|N/A) br=128000 ;; esac
  case "$rate" in ''|N/A) rate=44100 ;; esac
  ms=$(awk -v d="$dur" 'BEGIN{printf "%d", d*1000}')
  kb=$(awk -v b="$br" 'BEGIN{printf "%d", b/1000}')
  title=${t%% (*}
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$f" "$title" "$ARTIST" "$ALBUM" "$ms" "$kb" "$rate"
done
