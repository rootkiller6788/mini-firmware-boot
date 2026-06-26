import os
os.chdir(r"F:\nano-everything\mini-everything\2. mini-firmware-boot\mini-firmware-security")

# Check total lines so far
total = 0
for f in os.listdir("include"):
    if f.endswith(".h"):
        n = len(open(f"include/{f}").readlines())
        print(f"  include/{f}: {n} lines")
        total += n
for f in os.listdir("src"):
    if f.endswith(".c"):
        n = len(open(f"src/{f}").readlines())
        print(f"  src/{f}: {n} lines")
        total += n
print(f"Total include+src: {total} lines (need >= 3000)")
