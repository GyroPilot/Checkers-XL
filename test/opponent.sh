#!/bin/bash
# opponent.sh: the second seat of a network test, played with curl against the LOCAL table server (never Tim's).
#   bash test/opponent.sh [base url] [table] [name]
# Waits until the Mac has sat down (so the Mac is black in game 1), then: plays the first legal move whenever it is
# its turn; in game 1 accepts a draw when the Mac offers one (Command-E), and does not move while it is offered;
# after a game, asks for another (/again); in game 2 resigns once 5 moves are played; stops when the Mac has left.
B=${1:-http://127.0.0.1:8090}; TB=${2:-c1}; ME=${3:-curlbob}
q="table=$TB&player=$ME"
get() { curl -s --max-time 5 "$B$1?$q"; }
val() { printf '%s\n' "$S" | sed -n "s/^$1 //p" | head -1; }
say() { echo "$(date +%T) opponent: $*"; }
until curl -s "$B/tables?dev=1" | grep -q "^T|$TB|.*|1/2|"; do sleep 1; done
say "the Mac sat down; joining $TB"
game=1; again=0; seen_mac=0
for ((i = 0; i < 600; i++)); do
  S=$(get /state)
  st=$(val status); you=$(val you); turn=$(val turn); mv=$(val moves); oth=$( [ "$you" = black ] && val white || val black )
  [ "$oth" != "-" ] && [ -n "$oth" ] && seen_mac=1
  if [ $seen_mac = 1 ] && { [ "$oth" = "-" ] || [ -z "$oth" ]; }; then say "the Mac left; done"; get /leave >/dev/null; exit 0; fi
  if [ "$st" = over ]; then
    if [ $again = 0 ]; then say "game over: $(val result); /again"; get /again >/dev/null; again=1; fi
  elif [ "$st" = play ]; then
    if [ $again = 1 ]; then game=$((game + 1)); again=0; say "game $game: I am $you"; fi
    dr=$(val draw)
    if [ $game = 1 ] && [ "$dr" != "-" ] && [ -n "$dr" ] && [ "$dr" != "$you" ]; then say "the Mac offers a draw: accepting"; get /draw >/dev/null; continue; fi
    if [ $game = 2 ] && [ "${mv:-0}" -ge 5 ] && [ "$turn" = "$you" ]; then say "resigning"; get /resign >/dev/null; continue; fi
    if [ "$turn" = "$you" ]; then
      m=$(val legal | awk '{print $1}')
      say "move $m"; R=$(get "/move/$m"); printf '%s\n' "$R" | grep '^ERROR' && say "refused"
    fi
  fi
  sleep 2
done
