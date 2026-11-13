# The first Python program on Ulix (see chapter "Python" in lit/libc.nw). Uses only what is built into the interpreter.
import sys, os, math
print("hello from Python %d.%d on %s" % (sys.version_info[0], sys.version_info[1], sys.platform))
print("answer", 6 * 7, "sqrt(2) = %.6f" % math.sqrt(2))
print("cwd", os.getcwd(), "pid > 1:", os.getpid() > 1)
words = sorted({"ulix", "musl", "python"})
print(" ".join(words), len("".join(words)))
