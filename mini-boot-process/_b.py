import os
def w(p,c):
 os.makedirs(os.path.dirname(p),exist_ok=True)
 with open(p,"w") as f:f.write(c)
 print(f"OK {p}: {c.count(chr(10))} lines, {len(c)} chars")
