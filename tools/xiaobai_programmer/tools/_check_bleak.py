import sys
try:
    import bleak
    print("bleak", bleak.__version__, "| python", sys.version.split()[0])
except ImportError as exc:
    print("MISSING:", exc)
