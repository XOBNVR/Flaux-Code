"""Writes webview2_iids.h for installer/studio.cpp.

WebView2.h (from Microsoft's WebView2 SDK) declares an id for each of its
interfaces but leaves defining them to Microsoft's compiler. Built with gcc,
Flaux Studio.exe defines them itself: this reads every
MIDL_INTERFACE("id") Name : ... from WebView2.h and writes
    EXTERN_C const IID IID_Name = {...};
    template <> inline const IID &flaux_iid<Name>() { return IID_Name; }

    python installer/webview2_iids.py path/to/WebView2.h out/webview2_iids.h
"""

import re
import sys


def main(header, out):
    text = open(header, encoding="utf-8", errors="replace").read()
    found = re.findall(r'MIDL_INTERFACE\("([0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12})"\)\s*(\w+)\s*:', text)
    if len(found) < 50:
        sys.exit(f"Only found {len(found)} interfaces in {header}: has WebView2.h changed?")
    lines = ["/* Made by installer/webview2_iids.py from WebView2.h: the ids of WebView2's",
             "   interfaces, which a program built with gcc has to define itself. */",
             "#pragma once",
             "template <class I> const IID &flaux_iid();", ""]
    seen = set()
    for guid, name in found:
        if name in seen:
            continue
        seen.add(name)
        h = guid.replace("-", "")
        data4 = ", ".join("0x" + h[16 + 2 * i: 18 + 2 * i] for i in range(8))
        lines.append(f"EXTERN_C const IID IID_{name} = {{0x{h[:8]}, 0x{h[8:12]}, 0x{h[12:16]}, {{{data4}}}}};")
        lines.append(f"template <> inline const IID &flaux_iid<{name}>() {{ return IID_{name}; }}")
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"{out}: {len(seen)} interfaces")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
