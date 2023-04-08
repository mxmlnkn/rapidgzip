# xfce4-terminal --geometry 94x17
# asciinema rec --overwrite pragzip-comparison-2.rec -c 'bash show-benchmark.sh'

tmux new-session '
     ( echo '"'"'^[OSgzip ^M'"'"'; sleep 1h; ) | htop
' \; split-window -t top -l 10 '
    export PS1="$ "; bash --norc
' \; split-window -h '
    export PS1="$ "; bash --norc
' \; set-option status off \; select-pane -t top \; resize-pane -y 6

# time gzip -d -c silesia-20x.gz | wc -c
# time pragzip -d -c silesia-20x.gz | wc -c

# Get palette from ~/.config/xfce4/terminal/terminalrc (for some reason 4 twice as many digits per hex code, so reduce it)
# Append to .rec .jsonl file. Note that the first line may not contain newlines for formatting!
# "theme": {"fg": "#ffffff", "bg": "#000000", "palette": "#000000:#aa0000:#00aa00:#aa5500:#0000aa:#aa00aa:#00aaaa:#aaaaaa:#555555:#ff5555:#55ff55:#ffff55:#5555ff:#ff55ff:#55ffff:#ffffff"}
# The broken font sizes and line heights are necessary to reduce font rendering issues. Else, the tmux lines look bad.
# agg --font-family 'DejaVu Sans Mono' --font-size 13 --line-height 1.16 pragzip-comparison.{asciinema.jsonl,gif}
# Edit the asciinema recording:
#  - Remove everything after the first "exit" command
#  - Retime all setup stuff to happen at t = 0.0s instead of various t < 0.4s
# Further post-processing in Gimp:
#  - Remove the top 20 pixels:
#    1. Select all but those
#    2. Image -> Fit Canvas to Selection
#  - Round the edges:
#    1. Go to Background layer (a the bottom)
#    2. Right Mouse Button (RMB) -> Add Alpha Channel
#    3. Ctrl + A
#    4. Menu->Select->Rounded Rectangle->Radius (%): 10
#    5. Ctrl + I
#    6. Press Delete
#  - Export as GIF
#    1. Be sure to uncheck the comment and check the "Save as Animation"
