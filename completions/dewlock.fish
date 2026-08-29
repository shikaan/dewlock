# dewlock(1) completion

complete -c dewlock -l config     -s c --description "The config file to use." -r
complete -c dewlock -l debug      -s d --description "Enable debugging output."
complete -c dewlock -l daemonize  -s f --description "Detach from the controlling terminal after locking."
complete -c dewlock -l ready-fd   -s r --description "File descriptor to send readiness notifications to."
complete -c dewlock -l help       -s h --description "Show help message and quit."
complete -c dewlock -l version    -s v --description "Show the version number and quit."
