import sys, socket, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
from gdbpoke import GdbRemote
port = int(sys.argv[1])
g = GdbRemote("127.0.0.1", port, 50)
print("stub:", g.cmd("?"))
for a in (0x8be5d5de, 0x0be5d5de):
    print("Z3 %x ->" % a, g.cmd("Z3,%x,2" % a) or "(ok)")
for i in range(4):
    try:
        stop = g.cont()
    except socket.timeout:
        print("timeout after", i, "hits"); break
    r = g.regs()
    print("HIT pc=%08x stop=%s" % (r[15] or 0, stop))
g.cmd("D"); g.close()
