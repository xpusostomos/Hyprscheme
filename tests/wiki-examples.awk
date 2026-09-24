# wiki-examples.awk — extract every ```scheme block from the Hyprscheme wiki
# into per-block files for tests/t-zzz-docs.sh to evaluate.
#
#   awk -v out=DIR -v manifest=DIR/MANIFEST -f wiki-examples.awk ../Hyprscheme.wiki/*.md
#
# Each block is written to DIR/NNNN.scheme; MANIFEST lines are
#   NNNN <file> <line> <balanced:ok|BAD>
# The balance walk understands strings, ; line comments, #| |# block
# comments and #\ character literals — a block that ends off-depth is
# marked bad so the driver can fail it as a doc bug.

BEGIN { n = 0; inb = 0 }
/^```/ {
    if (!inb && $0 ~ /^```[ \t]*scheme[ \t]*$/) {
        inb = 1; depth = 0; instr = 0; inbc = 0; start = FNR; buf = ""
    } else if (inb) {
        inb = 0
        n++
        file = sprintf("%s/%04d.scheme", out, n)
        printf "%s\n", buf > file
        close(file)
        printf "%04d %s %d %s\n", n, FILENAME, start, (depth == 0 && !instr && !inbc) ? "ok" : "bad" >> manifest
    }
    next
}
inb {
    if (buf != "") buf = buf "\n"
    buf = buf $0
    # balance walk (mirrors the hand-run checker)
    line = $0
    i = 1
    while (i <= length(line)) {
        c = substr(line, i, 1)
        if (instr) {
            if (c == "\\") { i += 2; continue }
            if (c == "\"") instr = 0
            i++; continue
        }
        if (inbc) {
            if (substr(line, i, 2) == "|#") { inbc = 0; i += 2; continue }
            i++; continue
        }
        if (c == "\"") { instr = 1; i++; continue }
        if (substr(line, i, 2) == "#|") { inbc = 1; i += 2; continue }
        if (c == ";") break
        if (c == "#" && i < length(line) && substr(line, i + 1, 1) == "\\") {
            i += 2
            while (i <= length(line) && substr(line, i, 1) ~ /[A-Za-z0-9]/) i++
            continue
        }
        if (c == "(") depth++
        else if (c == ")") depth--
        i++
    }
}
END { if (inb) printf "wiki-examples.awk: unclosed ```scheme block in %s\n", FILENAME > "/dev/stderr" }
