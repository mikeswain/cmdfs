#!/usr/bin/python
import sys
import signal,syslog,os

def handler(signum, frame):
    syslog( 'Signal handler called with signal %d' % signum )
    raise IOError("Signal reeieved")

if 'INPUT_FILE' in os.environ:
    syslog.syslog("%s is %d" % (os.environ['INPUT_FILE'],os.stat(os.environ['INPUT_FILE']).st_size) )

for sig in [signal.SIGCHLD,signal.SIGABRT,signal.SIGINT]:
    signal.signal(sig, handler)

try:
    sys.stdout.write(sys.stdin.read());
   
except:
   syslog.syslog("Unexpected error:" + str(sys.exc_info()[0])) 
        