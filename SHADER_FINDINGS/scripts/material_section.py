"""Print the non-MSAA material section of a forward pixel program.

The section runs from the end of the MSAA coverage branch (`mov oMask, r…`) to the
first light-buffer or clustered-light read. Shared prologue and lighting code is
left out, so what remains is the material-specific texture combine.
"""
import re
import sys

lines = [re.sub(r"\[precise[^]]*\] ", "", l.rstrip()) for l in open(sys.argv[1])
         if not l.startswith("dcl")]
start = next((i for i, l in enumerate(lines) if re.search(r"mov oMask, r", l)), 0)
end = next((i for i in range(start, len(lines))
            if re.search(r"cb5\[168\]|cb4\[2\]|cb1\[31\]", lines[i])), len(lines))
print("\n".join(lines[start + 1:end]))
