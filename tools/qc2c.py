#!/usr/bin/env python3
"""Translate QuakeC bytecode (progs.dat) into C, ahead of time.

Each QuakeC function becomes one C function whose body is the same statements
the interpreter (engine/pr_exec.c PR_ExecuteProgram) would run, written as the
same C expressions in the same order, so results match the interpreter bit for
bit. Function entry/exit (locals save, parameter copy) and calls go through
the engine's own PR_EnterFunction/PR_LeaveFunction via PR_AOT_Call, so
recursion, builtins and dynamic calls (self.think and friends) behave exactly
as before.

The output is derived from id Software's game data, so it is generated at
build time from the user's own pak and never committed.

    python tools/qc2c.py --pak data/id1/pak0.pak -o build/progs_aot.c
    python tools/qc2c.py --stub -o build/progs_aot.c     # no data: empty table

Standard library only.
"""
import argparse
import os
import re
import struct
import sys

OPS = ("DONE MUL_F MUL_V MUL_FV MUL_VF DIV_F ADD_F ADD_V SUB_F SUB_V EQ_F EQ_V EQ_S EQ_E EQ_FNC "
       "NE_F NE_V NE_S NE_E NE_FNC LE GE LT GT LOAD_F LOAD_V LOAD_S LOAD_ENT LOAD_FLD LOAD_FNC "
       "ADDRESS STORE_F STORE_V STORE_S STORE_ENT STORE_FLD STORE_FNC STOREP_F STOREP_V STOREP_S "
       "STOREP_ENT STOREP_FLD STOREP_FNC RETURN NOT_F NOT_V NOT_S NOT_ENT NOT_FNC IF IFNOT CALL0 "
       "CALL1 CALL2 CALL3 CALL4 CALL5 CALL6 CALL7 CALL8 STATE GOTO AND OR BITAND BITOR").split()
OP = {n: i for i, n in enumerate(OPS)}


def fnv1a(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def read_pak_file(pak_path, name):
    d = open(pak_path, "rb").read()
    magic, dofs, dlen = struct.unpack_from("<4sii", d, 0)
    if magic != b"PACK":
        sys.exit("%s is not a pak file" % pak_path)
    for i in range(dlen // 64):
        n, ofs, ln = struct.unpack_from("<56sii", d, dofs + 64 * i)
        if n.split(b"\0")[0].decode("latin-1") == name:
            return d[ofs:ofs + ln]
    sys.exit("%s not found in %s" % (name, pak_path))


def struct_members(text, name):
    """(member, first word, words) for each member of the C struct `name`:
    float, int, string_t and func_t take one word, vec3_t three."""
    end = text.index("} %s;" % name)
    start = text.rindex("{", 0, end)
    out, ofs = [], 0
    for typ, member in re.findall(r"^\s*(float|int|string_t|func_t|vec3_t)\s+(\w+)\s*;", text[start:end], re.M):
        n = 3 if typ == "vec3_t" else 1
        out.append((member, ofs, n))
        ofs += n
    return out


def core_map(nfields):
    """QOG_EDICT_SPARSE: QuakeC field number -> word in entcore_t (engine/progs.h),
    or -1 for a field kept in the edict's sparse list. Both structs are read
    from the engine sources, so the table matches the build, and the engine
    checks it against its own map before it runs the compiled code."""
    eng = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "engine")
    entvars = {m: o for m, o, n in struct_members(open(os.path.join(eng, "progdefs.q1")).read(), "entvars_t")}
    m = [-1] * nfields
    for member, k, n in struct_members(open(os.path.join(eng, "progs.h")).read(), "entcore_t"):
        for w in range(n):
            m[entvars[member] + w] = k + w
    return m


def parse(progs):
    h = struct.unpack_from("<15i", progs, 0)
    (version, crc, ost, nst, ogd, ngd, ofd, nfd, ofn, nfn, ostr, nstr, oglob, nglob, entf) = h
    if version != 6:
        sys.exit("progs.dat version %d, expected 6" % version)
    stmts = [struct.unpack_from("<Hhhh", progs, ost + 8 * i) for i in range(nst)]
    globs = struct.unpack_from("<%di" % nglob, progs, oglob)
    # Field operands that are field definitions no statement writes: their
    # value is a constant field number, known now.
    fielddefs = set()
    for i in range(ngd):
        typ, ofs, _ = struct.unpack_from("<HHi", progs, ogd + 8 * i)
        if (typ & 0x7fff) == 5:
            fielddefs.add(ofs)
    written = set()
    for op, a, b, c in stmts:
        n = OPS[op] if op < len(OPS) else ""
        if n.startswith("STORE_"):
            written.add(b)
        elif n not in ("IF", "IFNOT", "GOTO", "RETURN", "DONE", "STATE") and not n.startswith("STOREP")                 and not n.startswith("CALL"):
            written.add(c)
    constfields = {g: globs[g] for g in fielddefs - written}
    funcs = []
    for i in range(nfn):
        first, parm_start, locs, prof, s_name, s_file, nparms = struct.unpack_from("<7i", progs, ofn + 36 * i)
        funcs.append((first, s_name))
    strings = progs[ostr:ostr + nstr]
    stmt_bytes = progs[ost:ost + 8 * nst]
    return dict(crc=crc, stmts=stmts, funcs=funcs, strings=strings, nglob=nglob,
                fingerprint=fnv1a(stmt_bytes), nst=nst, nfn=nfn, entf=entf, constfields=constfields)


def cstr(strings, ofs):
    end = strings.find(b"\0", ofs)
    s = strings[ofs:end].decode("latin-1")
    return "".join(c if c.isalnum() or c in "_." else "_" for c in s)[:48]


def G(o): return "G[%d]" % o
def I(o): return "I[%d]" % o


def field_ld(ctx, ed, b, k):
    """Word k of the field whose number is in global b, as a C int expression."""
    f = ctx["constfields"].get(b)
    if f is None:
        return "QC_LDX(%s, I[%d] + %d)" % (ed, b, k)
    return "QC_LD(%s, %d, %d)" % (ed, f + k, ctx["coremap"][f + k])


def emit_stmt(s, op, a, b, c, out, label, ctx):
    n = OPS[op] if op < len(OPS) else None
    w = out.append
    if n in ("ADD_F", "SUB_F", "MUL_F", "DIV_F"):
        sym = {"ADD_F": "+", "SUB_F": "-", "MUL_F": "*", "DIV_F": "/"}[n]
        w("%s = %s %s %s;" % (G(c), G(a), sym, G(b)))
    elif n in ("ADD_V", "SUB_V"):
        sym = "+" if n == "ADD_V" else "-"
        for k in range(3):
            w("%s = %s %s %s;" % (G(c + k), G(a + k), sym, G(b + k)))
    elif n == "MUL_V":
        w("%s = %s*%s + %s*%s + %s*%s;" % (G(c), G(a), G(b), G(a + 1), G(b + 1), G(a + 2), G(b + 2)))
    elif n == "MUL_FV":
        for k in range(3):
            w("%s = %s * %s;" % (G(c + k), G(a), G(b + k)))
    elif n == "MUL_VF":
        for k in range(3):
            w("%s = %s * %s;" % (G(c + k), G(b), G(a + k)))
    elif n in ("BITAND", "BITOR"):
        w("%s = (float)((int)%s %s (int)%s);" % (G(c), G(a), "&" if n == "BITAND" else "|", G(b)))
    elif n in ("GE", "LE", "GT", "LT", "AND", "OR"):
        sym = {"GE": ">=", "LE": "<=", "GT": ">", "LT": "<", "AND": "&&", "OR": "||"}[n]
        w("%s = (float)(%s %s %s);" % (G(c), G(a), sym, G(b)))
    elif n == "NOT_F":
        w("%s = (float)(!%s);" % (G(c), G(a)))
    elif n == "NOT_V":
        w("%s = (float)(!%s && !%s && !%s);" % (G(c), G(a), G(a + 1), G(a + 2)))
    elif n == "NOT_S":
        w("%s = (float)(!%s || !pr_strings[%s]);" % (G(c), I(a), I(a)))
    elif n == "NOT_FNC":
        w("%s = (float)(!%s);" % (G(c), I(a)))
    elif n == "NOT_ENT":
        w("%s = (float)(PROG_TO_EDICT(%s) == sv.edicts);" % (G(c), I(a)))
    elif n == "EQ_F":
        w("%s = (float)(%s == %s);" % (G(c), G(a), G(b)))
    elif n == "EQ_V":
        w("%s = (float)((%s == %s) && (%s == %s) && (%s == %s));" % (G(c), G(a), G(b), G(a + 1), G(b + 1), G(a + 2), G(b + 2)))
    elif n == "EQ_S":
        w("%s = (float)(!strcmp(pr_strings+%s, pr_strings+%s));" % (G(c), I(a), I(b)))
    elif n in ("EQ_E", "EQ_FNC"):
        w("%s = (float)(%s == %s);" % (G(c), I(a), I(b)))
    elif n == "NE_F":
        w("%s = (float)(%s != %s);" % (G(c), G(a), G(b)))
    elif n == "NE_V":
        w("%s = (float)((%s != %s) || (%s != %s) || (%s != %s));" % (G(c), G(a), G(b), G(a + 1), G(b + 1), G(a + 2), G(b + 2)))
    elif n == "NE_S":
        w("%s = (float)strcmp(pr_strings+%s, pr_strings+%s);" % (G(c), I(a), I(b)))
    elif n in ("NE_E", "NE_FNC"):
        w("%s = (float)(%s != %s);" % (G(c), I(a), I(b)))
    elif n in ("STORE_F", "STORE_ENT", "STORE_FLD", "STORE_S", "STORE_FNC"):
        w("%s = %s;" % (I(b), I(a)))
    elif n == "STORE_V":
        for k in range(3):
            w("%s = %s;" % (G(b + k), G(a + k)))
    elif n in ("STOREP_F", "STOREP_ENT", "STOREP_FLD", "STOREP_S", "STOREP_FNC", "STOREP_V"):
        nw = 3 if n == "STOREP_V" else 1
        prev = ctx.get("addr")
        f = ctx["constfields"].get(prev[2]) if prev else None
        if prev and prev[0] == b and prev[0] != prev[1] and f is not None:
            # right after the OP_ADDRESS that made the pointer: store straight
            # into that edict's field, where the pointer points
            w("{ edict_t *ed = PROG_TO_EDICT(%s);" % I(prev[1]))
            for k in range(nw):
                w("QC_ST(ed, %d, %d, %s);" % (f + k, ctx["coremap"][f + k], I(a + k)))
            w("}")
        else:
            w("QC_STP(%s, %s, %s, %s, %d);" % (I(b), I(a), I(a + 1) if nw == 3 else "0",
                                              I(a + 2) if nw == 3 else "0", nw))
    elif n == "ADDRESS":
        w("{ edict_t *ed = PROG_TO_EDICT(%s); if (ed == (edict_t *)sv.edicts && sv.state == ss_active) "
          "PR_RunError (\"assignment to world entity\"); %s = QC_ADDR(%s, ed, %s); }"
          % (I(a), I(c), I(a), I(b)))
    elif n in ("LOAD_F", "LOAD_FLD", "LOAD_ENT", "LOAD_S", "LOAD_FNC"):
        w("{ edict_t *ed = PROG_TO_EDICT(%s); %s = %s; }" % (I(a), I(c), field_ld(ctx, "ed", b, 0)))
    elif n == "LOAD_V":
        w("{ edict_t *ed = PROG_TO_EDICT(%s); %s = %s; %s = %s; %s = %s; }"
          % (I(a), I(c), field_ld(ctx, "ed", b, 0), I(c + 1), field_ld(ctx, "ed", b, 1),
             I(c + 2), field_ld(ctx, "ed", b, 2)))
    elif n == "IFNOT":
        w("if (!%s) goto %s;" % (I(a), label(s + b)))
    elif n == "IF":
        w("if (%s) goto %s;" % (I(a), label(s + b)))
    elif n == "GOTO":
        w("goto %s;" % label(s + a))
    elif n and n.startswith("CALL"):
        w("pr_argc = %d; PR_AOT_Call (%s);" % (op - OP["CALL0"], I(a)))
    elif n in ("DONE", "RETURN"):
        w("G[1] = %s; G[2] = %s; G[3] = %s; return;" % (G(a), G(a + 1), G(a + 2)))
    elif n == "STATE":
        w("{ edict_t *ed = PROG_TO_EDICT(pr_global_struct->self); "
          "ed->v.nextthink = pr_global_struct->time + 0.1; "
          "if (%s != ed->v.frame) ed->v.frame = %s; ed->v.think = %s; }" % (G(a), G(a), I(b)))
    else:
        w("PR_RunError (\"Bad opcode %d\");" % op)


HEADER = """/* Generated by tools/qc2c.py from %s -- do not edit, do not commit.
 * Derived from id Software's game data: each player generates it from their
 * own pak file at build time. */
#include "quakedef.h"

void PR_AOT_Call (func_t fnum);
#define G pr_globals
#define I ((int *)pr_globals)

/* Edict field access, the same as engine/pr_exec.c does it for each layout.
 * QC_LD and QC_ST take the field number and its entcore_t word (-1 for a
 * field in the sparse list), both constants here. */
#ifdef QOG_EDICT_SPARSE
#define QC_LD(ed,o,k)	((k) >= 0 ? ((int *)&(ed)->v)[(k) >= 0 ? (k) : 0] : ED_ColdGet (ed, o))
#define QC_ST(ed,o,k,x)	do { if ((k) >= 0) ((int *)&(ed)->v)[(k) >= 0 ? (k) : 0] = (x); else ED_ColdSet (ed, o, x); } while (0)
#define QC_LDX(ed,o)	ED_FieldGet (ed, o)
#define QC_ADDR(e,ed,o)	(((e) << 8) | (o))
#define QC_STP(p,x0,x1,x2,n)	do { edict_t *pe = PROG_TO_EDICT(((p) >> 8)); int po = (p) & 255; \
	ED_FieldSet (pe, po, x0); if ((n) == 3) { ED_FieldSet (pe, po + 1, x1); ED_FieldSet (pe, po + 2, x2); } } while (0)
#else
#define QC_LD(ed,o,k)	(((int *)&(ed)->v)[o])
#define QC_ST(ed,o,k,x)	(((int *)&(ed)->v)[o] = (x))
#define QC_LDX(ed,o)	(((int *)&(ed)->v)[o])
#define QC_ADDR(e,ed,o)	((byte *)((int *)&(ed)->v + (o)) - (byte *)sv.edicts)
#define QC_STP(p,x0,x1,x2,n)	do { int *pp = (int *)((byte *)sv.edicts + (p)); \
	pp[0] = (x0); if ((n) == 3) { pp[1] = (x1); pp[2] = (x2); } } while (0)
#endif
"""


def generate(p, src_name):
    stmts, funcs = p["stmts"], p["funcs"]
    starts = sorted(f[0] for f in funcs if f[0] > 0)
    nxt = {s: (starts[i + 1] if i + 1 < len(starts) else len(stmts)) for i, s in enumerate(starts)}
    out = [HEADER % src_name]
    names = []
    for fi, (first, s_name) in enumerate(funcs):
        if first <= 0:
            names.append(None)
            continue
        end = nxt[first]
        targets = set()
        for s in range(first, end):
            op, a, b, c = stmts[s]
            if op in (OP["IF"], OP["IFNOT"]):
                targets.add(s + b)
            elif op == OP["GOTO"]:
                targets.add(s + a)
        fname = "qc_%d" % fi
        names.append(fname)
        out.append("/* %s */\nstatic void %s (void)\n{" % (cstr(p["strings"], s_name), fname))
        body = []
        label = lambda t: "L%d" % t
        ctx = dict(constfields=p["constfields"], coremap=p["coremap"])
        for s in range(first, end):
            op, a, b, c = stmts[s]
            lines = []
            if s in targets:
                ctx["addr"] = None   # a jump lands here, the statement before may not have run
            emit_stmt(s, op, a, b, c, lines, label, ctx)
            ctx["addr"] = (c, a, b) if op == OP["ADDRESS"] else None
            prefix = ("L%d: " % s) if s in targets else "\t"
            body.append(prefix + " ".join(lines))
        if any(t >= end or t < first for t in targets):
            sys.exit("function %d jumps outside its body" % fi)
        out += body
        out.append("}\n")
    out.append("void (*const pr_aot_functions[%d]) (void) = {" % len(funcs))
    out.append(",\n".join("\t%s" % (n or "0") for n in names))
    out.append("};")
    out.append("const int pr_aot_numfunctions = %d;" % len(funcs))
    out.append("const int pr_aot_numstatements = %d;" % p["nst"])
    out.append("const unsigned pr_aot_fingerprint = 0x%08Xu;" % p["fingerprint"])
    out.append("/* QOG_EDICT_SPARSE: the entcore_t word of each field number that the code above assumes */")
    out.append("const int pr_aot_numfields = %d;" % p["entf"])
    out.append("const signed char pr_aot_coremap[%d] = {%s};\n" % (p["entf"], ",".join(str(x) for x in p["coremap"])))
    return "\n".join(out)


STUB = """/* Generated by tools/qc2c.py: no progs.dat was available, so QuakeC runs
 * in the interpreter. */
#include <stddef.h>
void (*const *const pr_aot_functions_stub) (void) = NULL;
void (*const pr_aot_functions[1]) (void) = { 0 };
const int pr_aot_numfunctions = 0;
const int pr_aot_numstatements = 0;
const unsigned pr_aot_fingerprint = 0u;
const int pr_aot_numfields = 0;
const signed char pr_aot_coremap[1] = { 0 };
"""


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pak", help="pak file holding progs.dat")
    ap.add_argument("--progs", help="a loose progs.dat instead of --pak")
    ap.add_argument("--stub", action="store_true", help="write an empty table")
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args(argv)
    if not a.stub and not a.progs and (not a.pak or not os.path.exists(a.pak)):
        a.stub = True
    if a.stub:
        text = STUB
        msg = "qc2c: no progs.dat, wrote the empty table"
    else:
        progs = open(a.progs, "rb").read() if a.progs else read_pak_file(a.pak, "progs.dat")
        p = parse(progs)
        p["coremap"] = core_map(p["entf"])
        text = generate(p, os.path.basename(a.progs or a.pak))
        msg = "qc2c: %d functions, %d statements -> %s" % (p["nfn"], p["nst"], a.out)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    old = open(a.out).read() if os.path.exists(a.out) else None
    if old != text:   # keep the timestamp when nothing changed
        with open(a.out, "w") as f:
            f.write(text)
    print(msg)
    return 0


if __name__ == "__main__":
    sys.exit(main())
