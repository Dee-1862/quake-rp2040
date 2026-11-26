"""Board cost model for timedemo demo3 (Agent AS).

Turns the per-frame work counts of a QOG_BOARDMODEL PC run (quakeog_pc
-bmlog, port/pc/boardmodel_pc.c) into board milliseconds per render phase
and a predicted board fps, for any clock and LCD SPI rate.

How it works
- Each count says which code path did the work. Each path has a priori
  costs (WEIGHTS below, from the agents' Thumb code reads in
  docs/changes.md): float helper calls and integer cycles per unit. A
  branch that swaps a float path for an integer one changes the counts.
- calibrate: two global factors, one for float calls (kf) and one for
  integer cycles (ki), are fitted to the board's nine phase times of the
  calibration run (commit de58316, Fast, 250 MHz). What the two factors
  leave per phase is kept as that phase's residual r, so the calibration
  run comes out exact and r says how well the weights explain the board.
- The frame is simulated frame by frame in board time: the time before the
  view, the view's waits for the last LCD scanout (VID_QogScanFree's byte
  ranges, then the whole scanout after R_RenderWorld), notify lines and
  centre prints (which make the view wait for the whole scanout as long as
  they show, in board time), then the rest of the view.
- predict: the same weights, kf, ki and r at another clock and SPI rate.
  The range (10 to 90%) comes from refitting with every weight scaled at
  random (x/ 1.65) and the time before the view drawn from 0.8 to 4 ms.

  python tools/boardmodel.py calibrate --log ref.csv -o tools/boardmodel_calib.json
  python tools/boardmodel.py predict --calib tools/boardmodel_calib.json --ref ref.csv
      --log fast.csv [--log turbo.csv] [--mhz 300] [--spi 50] [--names Fast,Turbo]
"""
import argparse, csv, gzip, json, math, random, statistics

# ---- what the board measured (calibration target) --------------------------
# de58316, timedemo demo3, Fast, 250 MHz, flash 125 MHz, LCD SPI 62.5 MHz
# (docs/changes.md, "Board results of the speed round")
BOARD_REF = {
    "commit": "de58316", "mhz": 250.0, "spi": 62.5, "frames": 1090, "seconds": 79.9,
    "phases": {"spans": 16.9, "lmap": 13.9, "world": 10.4, "edgescan": 7.2, "bmodels": 6.9,
               "models": 5.1, "vmodel": 3.4, "grad": 3.1, "parts": 1.2},
    "view_total": 72.2,
}
# E1M1 play, standing at the start, same build and session (frame, view,
# race from the [time cl] line, lmap from Agent AL's note). Its light grids
# have no dynamic lights, which splits demo3's lmap into the base grids and
# the dynamic light work, and its frame checks the race model.
BOARD_E1M1 = {"lmap": 2.0, "frame": 55.2, "view": 53.2, "race": 6.0}
LMAP_BASE = ("lms_builds", "lms_lux", "lms_stylelux", "lms_dlbuilds")
PHASES = ["world", "bmodels", "edgescan", "lmap", "grad", "spans", "models", "vmodel", "parts"]
OTHER = "other"             # setup, zspans, sky, turb: in the view total, not in the board's list
FCALL = 65.0                # a priori cycles of one float helper call through the ROM (55 to 75)

# ---- a priori cost per unit of work: (float helper calls, integer cycles) ---
WEIGHTS = {
    "world": {
        "wc_nodes": (0.3, 140),     # integer frustum test first (QOG_WORLDNODE_INT), float near a plane
        "wc_leafs": (0, 300),       # mark surfaces, efrags
        "wc_faces": (20, 300),      # R_RenderFace: plane side, Fast face gradients (16 float ops)
        "wc_clipedge": (0, 90),     # integer plane sides (QOG_FRONT_FAST)
        "wc_emitedge": (0.5, 120),  # fixed point edge setup
        "wc_cached": (0, 40),       # edge cache hit
        "wc_project": (0.2, 260),   # fixed point projection, float fallback
        "wc_clipdot": (0, 80),      # integer view plane distance
    },
    "bmodels": {
        "bm_bmcheck": (30, 300),    # R_BmodelCheckBBox (doubles: float compares with QOG_BMODEL_BBOXF)
        "bm_rotent": (200, 500),    # R_RotateBmodel with two R_TransformFrustum (80 float calls each) and R_QogFxSetup (about 7,500 cycles)
        "aq_bsurfs": (4, 30),       # brush surface facing test (a float dot product, f2d, dcmp)
        "bm_bmsplit": (6, 60),      # R_SplitEntityOnNode2 box against a world plane
        "bm_bmsplitint": (0, 40),   #   the same on an axial plane by integer compares (Agent AQ, QOG_BMSPLIT_INT)
        "bm_bpoly": (20, 150),      # R_RecursiveClipBPoly: the world plane into model space
        "bm_bpolyedge": (8, 80),    #   an edge against it (more when split)
        "bm_bmfaces": (20, 400),    # brush faces not clipped to the world
        "bm_bmemit": (0.5, 150),
        "bm_bmclip": (0, 100),
        "bm_bmmark": (8, 50),       # R_MarkLights on a brush entity's nodes
    },
    "edgescan": {
        "bm_scanlines": (0, 250),   # per scan line: insert, remove, flush checks
        "bm_scanedges": (0.2, 55),  # each active edge each line: spans, u step, a float 1/z on some overlaps
        "pc_litspans": (0, 40),     # span emission
        "bm_surfdrawn": (8, 200),   # D_DrawSurfaces per surface: mip level, state
        "bm_bmsurf": (150, 300),    # brush surface: two R_TransformFrustum and a TransformVector (Agent AQ: about 9,000 cycles)
        "aq_leadz": (20, 50),       # 1/z order of two brush surfaces in R_LeadingEdge (doubles, about 1,350 cycles)
    },
    "lmap": {
        "lms_builds": (2, 800),     # D_SetupLitSurface and the light grid setup
        "lms_lux": (0, 10),         # grid clear and conversion
        "lms_stylelux": (0, 30),    # base grid, about 30 cycles a cell style (Agent AL)
        "lms_dlbuilds": (0, 400),   # R_AddDynamicLights entry, R_DlightBits hash
        "lms_pairs": (6, 60),       # light plane distance and fabs
        "lms_pairsin": (14, 50),    # impact point and local coordinates
        "bm_dlpair_int": (15, 200), # QOG_DLIGHT_INT: the floats read once per pair
        "bm_dlcell_int": (0, 40),   # integer cell
        "bm_dlcell_flt": (5, 15),   # id's cell
        "bm_dllit_flt": (5, 10),    # id's lit cell, 5 more
        "lmap_marknodes": (8, 50),  # R_MarkLights node of the world pass (fewer floats with QOG_DLIGHT_AXIAL)
        "lms_marks": (0, 40),       # surface marked
        "lms_treenodes": (8, 50),   # R_LightReaches node
        "bm_reachup": (0, 400),     # R_QogLightReachesWorld binary search
        "bm_reachup_step": (2, 30), # one parent plane test
    },
    "grad": {
        "bm_grad_idd": (90, 300),   # id's D_CalcGradients: about 78 float calls, 6 double helpers, a divide
        "bm_grad_idf": (78, 300),   # without the doubles (QOG_GRAD_EXACTF)
        "bm_grad_fast": (15, 300),  # D_QogGradFast: about 15 float calls, integer dot products
    },
    "spans": {
        "pc_litpx": (0, 22),        # Fast pixel loop with the interpolators, z written (split over two cores)
        "pc_runs": (0, 55),         # sub-span step, one integer divide (split)
        "pc_litspans": (0, 400),    # fixed point span start (split)
        "pc_litsurfs": (2, 700),    # per surface setup, core 1 start and join (not split)
    },
    "models": {
        "am_check": (2, 2400), "am_drawn": (60, 2000), "am_verts": (0, 130), "am_tris": (0, 60),
        "am_smooth": (0, 450), "am_spans": (0, 80), "am_pix": (0, 35), "am_subcall": (0, 120),
        "am_subpix": (0, 30), "am_cliptri": (0, 1200),
        "am_subleaf": (0, 25),      # Agent AQ: a subdivision leaf tested inline (QOG_SUBDIV_LEAF), six compares
        "am_smalltri": (0, 150),    # Agent AQ: a triangle of two lines or less with no pixel or one, integer edge steps only (QOG_ALIAS_SMALLTRI)
        "am_tinytri": (0, 150),     # Agent AR: Turbo triangles within 3 x 3 pixels, about a third of a smooth one
    },
    "vmodel": {
        "vm_drawn": (60, 2000), "vm_smalltri": (0, 150), "vm_verts": (0, 130), "vm_tris": (0, 60), "vm_smooth": (0, 450),
        "vm_spans": (0, 80), "vm_pix": (0, 35), "vm_cliptri": (0, 1200),
    },
    "parts": {
        "am_parts": (12, 50),       # float particle physics
        "am_partvis": (0, 250),     # integer projection (QOG_ALIAS_FAST)
        "am_partpix": (0, 8),
    },
    OTHER: {
        "frame": (300, 5000),       # R_SetupFrame, R_MarkLeaves, view setup
        "pc_skypx": (0, 30),
        "pc_turbpx": (0, 25),
    },
}
SPLIT2 = {"pc_litpx", "pc_runs", "pc_litspans"}     # spans work shared by both cores
CORE_SHARE = 0.55           # the busier core's share of it
# weights that depend on how the build was compiled (#features line of the log)
FEATURE_SCALE = {
    "QOG_DLIGHT_AXIAL": {"lmap_marknodes": (5.0 / 8, 1.0), "lms_treenodes": (5.0 / 8, 1.0),
                         "bm_bmmark": (5.0 / 8, 1.0)},
    # Agent AQ's exact float cuts: the same counts, cheaper (docs/changes.md)
    "QOG_LEADZ_FLOAT": {"aq_leadz": (0.5, 1.0)},            # about 1,350 to 700 cycles
    "QOG_BPOLY_AXIAL": {"bm_bpoly": (0.05, 1.0), "bm_bpolyedge": (0.05, 1.0)},  # tplane 21 calls to 1, an edge 730 to 90 cycles
    "QOG_BMODEL_FACING": {"aq_bsurfs": (0.1, 1.0)},
    "QOG_BMODEL_BBOXF": {"bm_bmcheck": (0.35, 1.0)},        # about 330 cycles a plane less
    "QOG_BMODEL_FRUSTUM": {"bm_rotent": (0.6, 1.0)},        # 6.6 of 11 view plane sets from the world's normals
    "QOG_FX_REUSE": {"bm_rotent": (0.45, 1.0)},             # the Fast setup only for the origin, about 7,500 cycles less
    "QOG_DSURF_NOFRUSTUM": {"bm_bmsurf": (0.1, 1.0)},       # no view planes turned per brush surface
    "QOG_FGRAD_AXIAL": {"wc_faces": (0.77, 1.0)},           # about 300 cycles a face gradient less
    # Agent AR: the Q span drawer, the same spans, sub-spans and pixels for
    # fewer cycles. Its replay of demo3 surfaces through the board's own code
    # (docs/changes.md) gives 2.39 M against 3.69 M cycles a Fast view and
    # 1.47 M against 2.41 M a Turbo view, about 0.64 of every span count
    "QOG_LITSPAN_Q": {"pc_litpx": (1.0, 0.64), "pc_runs": (1.0, 0.64), "pc_litspans": (1.0, 0.64)},
    # the particle kill checks as integer compares: 4 of about 16 float helper
    # calls a particle gone (two float to double conversions, two double compares)
    "QOG_PART_DIEBITS": {"am_parts": (0.75, 1.0)},
    # Agent AQ's second round, the same counts with less integer work (Thumb
    # instruction counts of the hot paths before and after, docs/changes.md)
    "QOG_SCAN_FAST": {"bm_scanedges": (1.0, 0.7),           # one loop, no calls or global reloads per edge
                      "aq_leadz": (0.93, 1.0)},             #   16% of the 1/z compares reuse surf's half (5 of 11 float calls)
    "QOG_BPOLY_SIDES": {"bm_bpolyedge": (1.0, 0.5)},        # sides inline, a shared point's side kept
    "QOG_LMAP_FUSE": {"lms_lux": (1.0, 0.65), "lms_stylelux": (1.0, 0.65)},  # the grid's passes fused
    "QOG_BMODEL_BBOXINT": {"bm_bmcheck": (0.2, 1.0)},       # integer plane tests, floats only within 0.3 units of a plane
    "QOG_LEFTEDGE_LOCALS": {"am_spans": (1.0, 0.85), "vm_spans": (1.0, 0.85)},  # Agent AQ round 3: the left edge's rows without global stores
}
# Agent AM's pipeline (QOG_DRAW_CORE1): lit world surfaces drawn whole by either
# core, core 1 during the edge scan, both at each flush
CORE1_SPEED = 0.9           # core 1's speed against core 0's while both draw from the shared XIP cache (AM: up to 20% slower)
# Agent AP's PIO scanout: 5,120 bits a row plus about 0.4 us a line
LCD_LINE_US = 0.4

# ---- the LCD scanout ----------------------------------------------------------
# Agent V: 200 rows in 18.3 ms at 62.5 MHz SPI (91.5 us a row, 81.9 of them
# the bits), Agent AN: 22.4 ms at 50 MHz (112 us a row)
def row_ms(spi_mhz, mhz, lcd="pl022"):
    if lcd == "pio":    # Agent AP: no gaps between words, about 0.4 us a line
        return (320 * 16 / spi_mhz + LCD_LINE_US) / 1000.0
    return (320 * 16 / spi_mhz + 9.6 * 250.0 / mhz) / 1000.0


def pipeline(fr, m, s1):
    """core 0's time for the edge scan and the surfaces with Agent AM's
    pipeline, and core 0's wait for core 1's last surface"""
    e = m["edgescan"]
    sd = m["lmap"] + m["grad"]
    surfs = max(1.0, fr.get("pc_litsurfs", 1.0))
    px = max(1.0, fr.get("pc_litpx", 1.0))
    fs = min(1.0, fr.get("c1_surfs", 0.0) / surfs)
    fp = min(1.0, fr.get("c1_px", 0.0) / px)
    d = sd * fs + m["spans"] * fp               # pipelined world surfaces
    c0 = sd * (1 - fs) + m["spans"] * (1 - fp)  # sky, water and brush surfaces stay on core 0
    fe = fr.get("c1_earlypx", 0.0) / max(1.0, fr.get("c1_px", 0.0))
    during = min(e * s1, fe * d)                # core 1 during the scan
    r = d - during
    after = c0 if r <= s1 * c0 else (r + c0) / (1 + s1)
    n = max(1.0, fr.get("c1_surfs", 1.0))
    wait = 0.5 * d / n / s1 if d > 0 else 0.0
    return e + after + wait, wait, after

NOTIFY_S = 3.0              # con_notifytime
CENTER_S = 2.0              # scr_centertime
UNPACK_MS_250 = 0.3         # Agent AO: spreading a Turbo view for 2D over it
VIEW_ROWS = 152
W = 320
SETUP_SHARE = 0.3           # of "other", the part before the world (R_SetupFrame, R_MarkLeaves)
POST_MS = 0.3               # after the view: status bar repaint (mostly skipped), VID_Update
RECON = ("offset", "wait", "compute")


def load_log(path):
    feats = set()
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt", newline="") as f:
        first = f.readline()
        if first.startswith("#features"):
            feats = set(first.split()[1:])
        else:
            f.seek(0)
        rows = list(csv.DictReader(f))
    out = []
    for r in rows:
        d = {k: float(v) for k, v in r.items() if v not in (None, "")}
        d["frame"] = 1.0
        d["bm_rotent"] = max(0.0, d.get("wc_rotate", 0) - d.get("bm_bmsurf", 0))
        d["lmap_marknodes"] = max(0.0, d.get("lms_marknodes", 0) - d.get("bm_bmmark", 0))
        out.append(d)
    if len(out) > 2:
        out = out[1:]       # the timedemo's first frame holds the demo's start
    return out, feats


def weights_for(feats, scale=None, keymult=None):
    w = {}
    for ph, table in WEIGHTS.items():
        w[ph] = {}
        for k, (fc, ic) in table.items():
            for feat, adj in FEATURE_SCALE.items():
                if feat in feats and k in adj:
                    fc, ic = fc * adj[k][0], ic * adj[k][1]
            if scale:
                sf, si = scale.get((ph, k), (1.0, 1.0))
                fc, ic = fc * sf, ic * si
            if keymult and k in keymult:
                fc, ic = fc * keymult[k], ic * keymult[k]
            w[ph][k] = (fc, ic)
    return w


def frame_parts(frames, w, pipe=False):
    """per frame and phase: (float cycles a priori, integer cycles a priori).
    With pipe (QOG_DRAW_CORE1) the spans of a surface are one core's work."""
    share = 1.0 if pipe else CORE_SHARE
    out = []
    for fr in frames:
        d = {}
        for ph, table in w.items():
            f = i = 0.0
            for k, (fc, ic) in table.items():
                n = fr.get(k, 0.0) * (share if k in SPLIT2 else 1.0)
                f += n * fc * FCALL
                i += n * ic
            d[ph] = (f, i)
        out.append(d)
    return out


def mean_parts(parts):
    n = len(parts)
    return {ph: (sum(p[ph][0] for p in parts) / n, sum(p[ph][1] for p in parts) / n) for ph in parts[0]}


def fit_kf_ki(mp):
    """least squares of log(board / model) over the nine phases"""
    best = None
    grid = [0.25 * 1.06 ** j for j in range(80)]
    for kf in grid:
        for ki in grid:
            e = 0.0
            for ph in PHASES:
                f, i = mp[ph]
                m = (kf * f + ki * i) / (BOARD_REF["mhz"] * 1000.0)
                e += math.log(BOARD_REF["phases"][ph] / m) ** 2
            if best is None or e < best[0]:
                best = (e, kf, ki)
    return best[1], best[2], math.sqrt(best[0] / len(PHASES))


def to_ms(parts, cal, mhz):
    kf, ki, r = cal["kf"], cal["ki"], cal["r"]
    return [{ph: r.get(ph, 1.0) * (kf * p[ph][0] + ki * p[ph][1]) / (mhz * 1000.0) for ph in p} for p in parts]


def scan_need(lo, hi, rows, packed, first=0):
    """how far into its order the scanout in flight must be before no row it
    still sends reads a byte of [lo, hi) (VID_QogScanNeed). Agent AP: with
    first, rows [first, rows) go out first (QOG_VID_HUDFIRST)"""
    def pos(r):
        if not first:
            return r + 1
        return r - first + 1 if r >= first else rows - first + r + 1

    def maxpos(a, b):
        m = pos(b)
        if first and a < first:
            m = max(m, pos(min(b, first - 1)))
        return m
    end = rows * W
    need = 0
    if hi <= 0 or hi <= lo:
        return 0
    lo = max(lo, 0)
    p = packed
    if lo < p * (W // 2):
        need = maxpos(lo // (W // 2), (min(hi, p * (W // 2)) - 1) // (W // 2))
    if hi > p * W and lo < end and end > p * W:
        need = max(need, maxpos(max(lo, p * W) // W, (min(hi, end) - 1) // W))
    return need


def simulate(frames, ms, turbo, mhz, spi, pre_ms, post_ms, wscale=1.0, whatif=(), opts=None):
    """board time of each frame (ms) and the mean waits. wscale scales every
    wait (the "wait" reconciliation, see calibrate). opts: lcd ("pl022" or
    "pio"), pipe (Agent AM's pipeline), text (Agent AP's Turbo text list),
    s1 (core 1's speed)"""
    opts = opts or {}
    rms = row_ms(spi, mhz, opts.get("lcd", "pl022"))
    pipe, text, s1 = opts.get("pipe", False), opts.get("text", False), opts.get("s1", CORE1_SPEED)
    # Agent AP's race package: notify clears the view draws over are skipped
    # (a centre print erase still waits in Turbo, its rows lie below the
    # packed pixels), the brush entities run before the wait for the end
    # with a row wait for their clip buffers, and the status bar rows go
    # out first
    covered, early_b, hudfirst = opts.get("covered", False), opts.get("early_b", False), opts.get("hudfirst", False)
    t_now = 0.0
    notes = []          # board times of console lines and centre prints
    prev_packed = 0
    total = []
    waits = {"race_view": 0.0, "race_world": 0.0, "race_done": 0.0, "notify": 0.0, "c0wait": 0.0,
             "surf_c0": 0.0}
    notify_frames = 0
    for fr, m in zip(frames, ms):
        if fr.get("conlines", 0) > 0:
            notes.append((t_now, NOTIFY_S))
        if fr.get("center", 0) > 0:
            notes.append((t_now, CENTER_S))
        notes = [n for n in notes if t_now - n[0] < n[1]]
        notify = bool(notes) and "nonotify" not in whatif
        if covered:
            notify = notify and turbo and any(n[1] == CENTER_S for n in notes)
        rows_prev = int(fr.get("racerows", 200))
        first = VIEW_ROWS if hudfirst and rows_prev > VIEW_ROWS else 0
        t_scan = rows_prev * rms
        t = pre_ms
        if "norace" in whatif:
            t_scan = 0.0
        if notify:      # SCR_SetUpToDrawConsole waits for the whole scanout before the view
            notify_frames += 1
            w = max(0.0, t_scan - t) * wscale
            waits["notify"] += w
            t += w + m[OTHER] * SETUP_SHARE + m["world"]
        else:
            t += m[OTHER] * SETUP_SHARE
            ranges = [(int(fr.get("sf%dlo" % k, 0)), int(fr.get("sf%dhi" % k, 0))) for k in range(4)]
            ranges = [x for x in ranges if x[1] > 0]
            if "noedgetable" in whatif:     # the edge table kept clear of the status bar rows
                ranges = [x for x in ranges if x[0] < VIEW_ROWS * W]
            if "norace" in whatif:
                ranges, t_scan = [], 0.0
            late = [x for x in ranges if early_b and x[0] == VIEW_ROWS * W]    # the brush clip buffers
            ranges = [x for x in ranges if x not in late]
            for j, (lo, hi) in enumerate(ranges):
                need = scan_need(lo, hi, rows_prev, prev_packed, first)
                if need <= 0:
                    continue
                ready = t_scan if need >= rows_prev else need * rms
                if ready > t:
                    # the vertex cache's wait (the last range) is inside the world phase timer
                    key = "race_world" if j == len(ranges) - 1 and len(ranges) >= 3 else "race_view"
                    waits[key] += (ready - t) * wscale
                    t += (ready - t) * wscale
            t += m["world"]
            for lo, hi in late:
                need = scan_need(lo, hi, rows_prev, prev_packed, first)
                ready = t_scan if need >= rows_prev else need * rms
                if need > 0 and ready > t:
                    waits["race_done"] += (ready - t) * wscale
                    t += (ready - t) * wscale
            early = [x.split(":")[1] for x in whatif if x.startswith("early:")]
            if early_b and "bmodels" not in early:
                early.append("bmodels")
            t += sum(m[p] for p in early)      # work moved before the wait for the scanout's end
            if t_scan > t:                   # VID_QogScanDone after R_RenderWorld
                waits["race_done"] += (t_scan - t) * wscale
                t += (t_scan - t) * wscale
        done = ["world"] + ([] if notify else [x.split(":")[1] for x in whatif if x.startswith("early:")])
        if early_b and not notify and "bmodels" not in done:
            done.append("bmodels")
        if pipe:
            pt, pw, pa = pipeline(fr, m, s1)
            waits["c0wait"] += pw
            waits["surf_c0"] += pa
            t += pt
            done += ["edgescan", "lmap", "grad", "spans"]
        t += sum(m[p] for p in PHASES if p not in done) + m[OTHER] * (1 - SETUP_SHARE) + post_ms
        packed = 0
        if turbo:
            if notify:
                t += UNPACK_MS_250 * 250.0 / mhz   # the notify clear spreads the last packed view
                if text:    # Agent AP: the notify characters go to the scanout's list, the view stays packed
                    packed = VIEW_ROWS
            else:
                packed = VIEW_ROWS
        prev_packed = packed
        total.append(t)
        t_now += t / 1000.0
    n = len(frames)
    return total, {k: v / n for k, v in waits.items()}, notify_frames / n


def mean_ms(ms):
    return {ph: sum(m[ph] for m in ms) / len(ms) for ph in ms[0]}


def lmap_split(frames, feats, e1m1, scale, kf, ki):
    """multipliers of lmap's base grid and dynamic light weights from demo3 and E1M1"""
    def parts_of(fr_list, fe):
        w = weights_for(fe, scale)
        b = d = 0.0
        for k, (fc, ic) in w["lmap"].items():
            n = sum(fr.get(k, 0.0) for fr in fr_list) / len(fr_list)
            c = n * (kf * fc * FCALL + ki * ic) / (BOARD_REF["mhz"] * 1000.0)
            if k in LMAP_BASE:
                b += c
            else:
                d += c
        return b, d
    bd, dd = parts_of(frames, feats)
    be, de = parts_of(*e1m1)
    det = be * dd - de * bd
    x = (BOARD_E1M1["lmap"] * dd - de * BOARD_REF["phases"]["lmap"]) / det
    y = (be * BOARD_REF["phases"]["lmap"] - bd * BOARD_E1M1["lmap"]) / det
    x, y = max(0.05, x), max(0.05, y)
    return {k: (x if k in LMAP_BASE else y) for k in WEIGHTS["lmap"]}


def calibrate(frames, feats, scale=None, pre=None, e1m1=None):
    """kf, ki, r per phase, and the time before (pre) and after (post) the view at 250 MHz"""
    w = weights_for(feats, scale)
    parts = frame_parts(frames, w)
    mp = mean_parts(parts)
    kf, ki, rms_log = fit_kf_ki(mp)
    mhz, spi = BOARD_REF["mhz"], BOARD_REF["spi"]
    keymult = lmap_split(frames, feats, e1m1, scale, kf, ki) if e1m1 else None
    if keymult:
        w = weights_for(feats, scale, keymult)
        parts = frame_parts(frames, w)

    def build(pre_ms):
        cal = {"kf": kf, "ki": ki, "r": {ph: 1.0 for ph in w}}
        base = mean_ms(to_ms(parts, cal, mhz))
        for ph in PHASES:
            cal["r"][ph] = BOARD_REF["phases"][ph] / base[ph]
        rs = sorted(cal["r"][p] for p in PHASES)
        cal["r"][OTHER] = rs[len(rs) // 2]
        for _ in range(3):  # the board's world time holds the vertex cache race wait
            ms = to_ms(parts, cal, mhz)
            _, waits, _ = simulate(frames, ms, False, mhz, spi, pre_ms, 0.0)
            target = max(0.5, BOARD_REF["phases"]["world"] - waits["race_world"])
            cal["r"]["world"] = cal["r"]["world"] * target / mean_ms(ms)["world"]
        ms = to_ms(parts, cal, mhz)
        tot, waits, nf = simulate(frames, ms, False, mhz, spi, pre_ms, 0.0)
        m = mean_ms(ms)
        vt = sum(m[p] for p in PHASES) + m[OTHER] + waits["race_view"] + waits["race_done"]
        return cal, vt, tot, waits, nf

    solved = True
    if pre is None:     # the pre that makes the model's view total the board's
        lo, hi = 0.3, 8.0
        if build(lo)[1] < BOARD_REF["view_total"]:
            pre, solved = lo, False
        elif build(hi)[1] > BOARD_REF["view_total"]:
            pre, solved = hi, False
        else:
            for _ in range(16):
                mid = (lo + hi) / 2
                if build(mid)[1] > BOARD_REF["view_total"]:
                    lo = mid
                else:
                    hi = mid
            pre = (lo + hi) / 2
    cal, vt, tot, waits, nf = build(pre)
    target = BOARD_REF["seconds"] * 1000.0 / BOARD_REF["frames"]
    excess = statistics.mean(tot) + POST_MS - target
    # The board's view total (72.2) and timedemo frame (73.3) leave about 1 ms
    # outside the view, while the scanout waits the code implies outside the
    # view (notify text) come to more. Three ways to reconcile, all kept:
    # wait (every wait scaled down), compute (every phase scaled down, the
    # waits as the code implies), offset (a constant taken off each frame).
    recon = {"offset": {"post": target - statistics.mean(tot), "wscale": 1.0, "cscale": 1.0}}
    if excess <= 0:
        recon["wait"] = dict(recon["offset"])
        recon["compute"] = dict(recon["offset"])
    else:
        ms = to_ms(parts, cal, mhz)
        lo, hi = 0.0, 1.0
        for _ in range(20):
            mid = (lo + hi) / 2
            t2, _, _ = simulate(frames, ms, False, mhz, spi, pre, POST_MS, mid)
            if statistics.mean(t2) > target:
                hi = mid
            else:
                lo = mid
        recon["wait"] = {"post": POST_MS, "wscale": (lo + hi) / 2, "cscale": 1.0}
        lo, hi = 0.5, 1.0
        for _ in range(20):
            mid = (lo + hi) / 2
            msc = [{ph: v * mid for ph, v in m.items()} for m in ms]
            t2, _, _ = simulate(frames, msc, False, mhz, spi, pre * mid, POST_MS)
            if statistics.mean(t2) > target:
                hi = mid
            else:
                lo = mid
        recon["compute"] = {"post": POST_MS, "wscale": 1.0, "cscale": (lo + hi) / 2}
    cal["keymult"] = keymult
    cal.update({"pre_ms_250": pre, "pre_solved": solved, "fit_rms_log": rms_log, "recon": recon,
                "frame_model_ref": statistics.mean(tot), "view_total_model": vt, "waits_ref": waits,
                "notify_frac_ref": nf})
    return cal


def predict(frames, feats, cal, mhz, spi, modes=RECON, whatif=(), lcd="pl022"):
    turbo = statistics.mean(fr.get("rfast", 1) for fr in frames) >= 1.5
    pipe = "QOG_DRAW_CORE1" in feats and "nopipe" not in whatif
    opts = {"lcd": lcd, "pipe": pipe, "text": "QOG_TURBO_TEXT" in feats, "s1": cal.get("_s1", CORE1_SPEED),
            "covered": "QOG_NOTIFY_COVERED" in feats, "early_b": "QOG_BMODELS_EARLY" in feats,
            "hudfirst": "QOG_VID_HUDFIRST" in feats}
    parts = frame_parts(frames, weights_for(feats, cal.get("_scale"), cal.get("keymult")), pipe)
    ms = to_ms(parts, cal, mhz)
    for wi in whatif:           # phase*factor: a phase made that much cheaper or dearer
        if "*" in wi:
            ph, fac = wi.split("*")
            ms = [dict(m, **{ph: m[ph] * float(fac)}) for m in ms]
    a = cal.get("_clock", 1.0)  # share of the time that scales with the clock (samples only)
    if a != 1.0:
        ms = [{ph: v * (a + (1 - a) * mhz / 250.0) for ph, v in m.items()} for m in ms]
    f = 250.0 / mhz
    res = {"turbo": turbo, "modes": {}}
    if "offset" not in modes:
        modes = ("offset",) + tuple(modes)
        keep = modes[1:]
    else:
        keep = modes
    for mode in modes:
        rc = cal["recon"][mode]
        c = rc["cscale"]
        msc = ms if c == 1.0 else [{ph: v * c for ph, v in m.items()} for m in ms]
        total, waits, nf = simulate(frames, msc, turbo, mhz, spi, cal["pre_ms_250"] * f * c,
                                    rc["post"] * f, rc["wscale"], whatif, opts)
        res["modes"][mode] = statistics.mean(total)
        if mode == "offset":    # phases and waits as the code implies, for the tables
            mp = mean_ms(ms)
            mp["world"] += waits["race_world"]      # as the board's world timer shows it
            res.update({"phases": mp, "waits": waits, "notify_frac": nf})
    res["modes"] = {m: v for m, v in res["modes"].items() if m in keep}
    fm = statistics.mean(res["modes"].values())
    res.update({"frame_ms": fm, "fps": 1000.0 / fm})
    return res


def do_calibrate(a):
    frames, feats = load_log(a.log)
    e1m1 = None
    if a.e1m1:
        ef, efe = load_log(a.e1m1)
        e1m1 = (ef[a.e1m1_skip:], efe)
    cal = calibrate(frames, feats, e1m1=e1m1)
    cal.update({"ref": BOARD_REF, "features": sorted(feats), "log": a.log, "e1m1_log": a.e1m1,
                "e1m1_skip": a.e1m1_skip, "e1m1_board": BOARD_E1M1})
    with open(a.out, "w") as f:
        json.dump(cal, f, indent=1, sort_keys=True)
    mp = mean_parts(frame_parts(frames, weights_for(feats)))
    if cal["keymult"]:
        km = cal["keymult"]
        print("lmap split by E1M1: base grid weights x %.2f, dynamic light weights x %.2f"
              % (km["lms_stylelux"], km["lms_pairs"]))
    print("calibrated on %d frames of %s" % (len(frames), a.log))
    print("kf %.2f (float calls), ki %.2f (integer cycles), fit error %.0f%% rms over the nine phases"
          % (cal["kf"], cal["ki"], 100 * (math.exp(cal["fit_rms_log"]) - 1)))
    print("%-9s %7s %9s %9s %7s" % ("phase", "board", "two-k", "float%", "r"))
    for ph in PHASES:
        f, i = mp[ph]
        two = (cal["kf"] * f + cal["ki"] * i) / 250000.0
        print("%-9s %7.1f %9.1f %8.0f%% %7.2f" % (ph, BOARD_REF["phases"][ph], two,
                                                 100 * cal["kf"] * f / (cal["kf"] * f + cal["ki"] * i), cal["r"][ph]))
    w = cal["waits_ref"]
    rc = cal["recon"]
    print("before the view %.2f ms (%s); race waits before the world %.2f (of it in world %.2f), "
          "after the world %.2f, notify %.2f; notify frames %.0f%%"
          % (cal["pre_ms_250"], "solved" if cal["pre_solved"] else "at the bound",
             w["race_view"], w["race_world"], w["race_done"], w["notify"], 100 * cal["notify_frac_ref"]))
    print("model frame %.1f ms against the board's %.1f: offset %.1f ms, or waits x %.2f, or phases x %.3f"
          % (cal["frame_model_ref"], BOARD_REF["seconds"] * 1000.0 / BOARD_REF["frames"], rc["offset"]["post"],
             rc["wait"]["wscale"], rc["compute"]["cscale"]))


def do_predict(a):
    with open(a.calib) as f:
        cal = json.load(f)
    rframes, rfeats = load_log(a.ref or cal["log"])
    e1m1 = None
    if cal.get("e1m1_log"):
        ef, efe = load_log(cal["e1m1_log"])
        e1m1 = (ef[cal.get("e1m1_skip", 0):], efe)
    names = a.names.split(",") if a.names else ["run%d" % i for i in range(len(a.log))]
    rng = random.Random(1)
    keys = [(ph, k) for ph, t in WEIGHTS.items() for k in t]
    samples = []
    for _ in range(a.samples):
        scale = {key: (math.exp(rng.gauss(0, 0.5)), math.exp(rng.gauss(0, 0.5))) for key in keys}
        c = calibrate(rframes, rfeats, scale, rng.uniform(0.8, 4.0), e1m1)
        c["_scale"] = scale
        c["_clock"] = rng.uniform(0.94, 1.0)
        c["_s1"] = rng.uniform(0.8, 1.0)
        samples.append(c)
    out = {"mhz": a.mhz, "spi": a.spi, "lcd": a.lcd, "runs": {}}
    for name, path in zip(names, a.log):
        frames, feats = load_log(path)
        frames = frames[a.skip:]
        point = predict(frames, feats, cal, a.mhz, a.spi, lcd=a.lcd)
        sp = [predict(frames, feats, c, a.mhz, a.spi, (RECON[i % 3],), lcd=a.lcd) for i, c in enumerate(samples)]
        rng_ = {}
        for key in list(point["phases"]) + ["frame_ms", "fps"]:
            vals = sorted(s["phases"][key] if key in s["phases"] else s[key] for s in sp)
            rng_[key] = (vals[int(0.1 * (len(vals) - 1))], vals[int(0.9 * (len(vals) - 1))])
        point["range"] = rng_
        out["runs"][name] = point
    print_table(out, cal)
    if a.whatif:
        print("What if (central fps, frame ms, ms saved against each run as it is):")
        for spec in a.whatif:
            label, items = spec.split("=", 1)
            items = tuple(x for x in items.split(",") if x)
            line = "  %-28s" % label
            for name, path in zip(names, a.log):
                frames, feats = load_log(path)
                frames = frames[a.skip:]
                r = predict(frames, feats, cal, a.mhz, a.spi, whatif=items, lcd=a.lcd)
                base = out["runs"][name]["frame_ms"]
                line += "  %s %.1f fps %.1f ms (-%.1f)" % (name, r["fps"], r["frame_ms"], base - r["frame_ms"])
                out["runs"][name].setdefault("whatif", {})[label] = r["frame_ms"]
            print(line)
    if a.json:
        with open(a.json, "w") as f:
            json.dump(out, f, indent=1)


def print_table(out, cal):
    names = list(out["runs"])
    ref = cal["ref"]["phases"]
    print("Predicted board ms per view, timedemo demo3, %g MHz, LCD %g MHz by %s (10 to 90%% range)"
          % (out["mhz"], out["spi"], out.get("lcd", "pl022")))
    hdr = "%-34s %8s" % ("phase", "de58316")
    for n in names:
        hdr += " %22s" % n
    print(hdr)
    for ph in PHASES + [OTHER]:
        line = "%-34s %8s" % (ph + (" (with its race wait)" if ph == "world" else ""),
                              ("%.1f" % ref[ph]) if ph in ref else "")
        for n in names:
            r = out["runs"][n]
            lo, hi = r["range"][ph]
            line += " %8.1f (%4.1f to %4.1f)" % (r["phases"][ph], lo, hi)
        print(line)
    for key, label in (("race_view", "race wait before the world"), ("race_done", "race wait after the world"),
                       ("notify", "whole scanout waits (notify text)"),
                       ("surf_c0", "surfaces after the scan, core 0 clock"),
                       ("c0wait", "core 0 waits for core 1's last surface")):
        line = "%-34s %8s" % (label, "")
        for n in names:
            line += " %8.1f %13s" % (out["runs"][n]["waits"][key], "")
        print(line)
    line = "%-34s %8s" % ("frames with notify text", "")
    for n in names:
        line += " %7.0f%% %13s" % (100 * out["runs"][n]["notify_frac"], "")
    print(line)
    line = "%-34s %8.1f" % ("frame", 1000.0 * cal["ref"]["seconds"] / cal["ref"]["frames"])
    for n in names:
        r = out["runs"][n]
        lo, hi = r["range"]["frame_ms"]
        line += " %8.1f (%4.1f to %4.1f)" % (r["frame_ms"], lo, hi)
    print(line)
    line = "%-34s %8.1f" % ("fps", cal["ref"]["frames"] / cal["ref"]["seconds"])
    for n in names:
        r = out["runs"][n]
        lo, hi = r["range"]["fps"]
        line += " %8.1f (%4.1f to %4.1f)" % (r["fps"], lo, hi)
    print(line)
    line = "%-34s %8s" % ("fps by reconciliation (off/wait/cmp)", "")
    for n in names:
        md = out["runs"][n]["modes"]
        line += " %22s" % ("/".join("%.1f" % (1000.0 / md[m]) for m in RECON))
    print(line)
    for n in names:
        r = out["runs"][n]
        print("BOARDBENCH %s fps %.1f (range %.1f to %.1f) frame %.1f ms"
              % (n, r["fps"], r["range"]["fps"][0], r["range"]["fps"][1], r["frame_ms"]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("calibrate")
    c.add_argument("--log", required=True)
    c.add_argument("-o", "--out", required=True)
    c.add_argument("--e1m1", help="log of E1M1 standing at the start (-bmall), same build as --log")
    c.add_argument("--e1m1-skip", type=int, default=100)
    p = sub.add_parser("predict")
    p.add_argument("--calib", required=True)
    p.add_argument("--ref", help="the calibration log (default: the path in the calibration file)")
    p.add_argument("--log", action="append", required=True)
    p.add_argument("--names")
    p.add_argument("--mhz", type=float, default=300.0)
    p.add_argument("--spi", type=float, default=50.0, help="LCD bit rate in MHz")
    p.add_argument("--lcd", choices=("pl022", "pio"), default="pl022", help="LCD scanout by the SPI peripheral or PIO (Agent AP)")
    p.add_argument("--samples", type=int, default=30)
    p.add_argument("--json")
    p.add_argument("--skip", type=int, default=0, help="frames to leave out at the start of each log")
    p.add_argument("--whatif", action="append", default=[],
                   help="name=item,item: nonotify, noedgetable, norace or phase*factor; prints the fps it gives")
    a = ap.parse_args()
    if a.cmd == "calibrate":
        do_calibrate(a)
    else:
        do_predict(a)


if __name__ == "__main__":
    main()
