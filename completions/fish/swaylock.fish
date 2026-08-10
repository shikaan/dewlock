# swaylock(1) completion

complete -c swaylock -l config     -s c --description "The config file to use." -r
complete -c swaylock -l debug      -s d --description "Enable debugging output."
complete -c swaylock -l daemonize  -s f --description "Detach from the controlling terminal after locking."
complete -c swaylock -l ready-fd   -s r --description "File descriptor to send readiness notifications to."
complete -c swaylock -l help       -s h --description "Show help message and quit."
complete -c swaylock -l version    -s v --description "Show the version number and quit."
