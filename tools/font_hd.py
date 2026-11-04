"""HD font atlases: real outlines placed into the unchanged Xbox glyph cells.

Bicubic upscaling plus a threshold rounds every corner and turns the 4-bit
DXT3 coverage steps into wavy italic stems. Instead, every glyph is redrawn
from an outline and anti-aliased once, at the final HD size:

* `Outline font` - an open font that matches the source face (Liberation Sans
  for Arial, SIL OFL 1.1).
* `Donor atlas` - the same face rasterised larger in another game atlas
  (Conduit 21 px for Conduit 14 px), traced and scaled down.
* `trace` - the glyph's own coverage: a subpixel iso-contour, split at corners,
  fitted with straight lines and cubic Beziers; adjacent tangents meet in a
  sharp corner again.

A candidate glyph is aligned to the source cell by least squares against the
box-filtered HD image. Glyphs that fit badly, are missing or would be clipped
by their cell keep the traced version. Cell rectangles, offsets, advances and
kerning in the FNTX table are not modified.
"""
import io
import math
import struct
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from nfs_assets import find_chunks

try:
    import cairosvg
except ImportError:  # imported by the caller after loading libcairo
    cairosvg = None


# --- FNTX glyph table ---------------------------------------------------------

def font_tables(data):
    """Map lower-case font name to its glyph cells for every 0x30201 chunk."""
    fonts = {}
    for ident, pos, stop in find_chunks(data):
        if ident != 0x30201:
            continue
        name = data[pos+8:pos+0x48].split(b'\0')[0].decode('ascii').lower()
        table = data.find(b'FNTX', pos, stop)
        if table < 0:
            raise ValueError('Font chunk without FNTX: ' + name)
        count, = struct.unpack_from('<H', data, table+10)
        glyph_start, = struct.unpack_from('<I', data, table+20)
        if table + glyph_start + count*16 > stop:
            raise ValueError('Truncated FNTX glyph table: ' + name)
        glyphs = []
        for i in range(count):
            ch, w, h, x, y = struct.unpack_from('<HBBHH', data, table+glyph_start+i*16)
            if w and h:
                xoff, yoff = struct.unpack_from('<bb', data, table+glyph_start+i*16+9)
                advance, = struct.unpack_from('<H', data, table+glyph_start+i*16+14)
                glyphs.append({'ch': ch, 'x': x, 'y': y, 'w': w, 'h': h, 'xoff': xoff, 'yoff': yoff, 'advance': advance})
        fonts[name] = glyphs
    return fonts


# --- Traced outlines ------------------------------------------------------------

def _contours(a, level=0.5):
    """Marching squares through pixel centres; closed loops with ink on the left."""
    a = np.pad(a, 1)
    inside = a >= level
    links = {}
    for y, x in zip(*np.nonzero(inside[:-1, :-1] | inside[1:, :-1] | inside[:-1, 1:] | inside[1:, 1:])):
        tl, tr, br, bl = inside[y, x], inside[y, x+1], inside[y+1, x+1], inside[y+1, x]
        code = tl*8 + tr*4 + br*2 + bl
        if code == 15:
            continue
        T, R, B, L = ('h', y, x), ('v', y, x+1), ('h', y+1, x), ('v', y, x)
        segs = {1: [(L, B)], 2: [(B, R)], 3: [(L, R)], 4: [(R, T)], 6: [(B, T)], 7: [(L, T)],
                8: [(T, L)], 9: [(T, B)], 11: [(T, R)], 12: [(R, L)], 13: [(R, B)], 14: [(B, L)]}.get(code)
        if segs is None:
            centre = a[y:y+2, x:x+2].mean() >= level
            if code == 5:
                segs = [(L, T), (R, B)] if centre else [(L, B), (R, T)]
            else:
                segs = [(T, R), (B, L)] if centre else [(T, L), (B, R)]
        for start, end in segs:
            links[start] = end

    def point(key):
        kind, y, x = key
        if kind == 'h':
            v0, v1 = a[y, x], a[y, x+1]
            return x + (level-v0)/(v1-v0) - 1, y - 1
        v0, v1 = a[y, x], a[y+1, x]
        return x - 1, y + (level-v0)/(v1-v0) - 1

    loops, seen = [], set()
    for start in sorted(links):
        loop, key = [], start
        while key not in seen:
            seen.add(key)
            loop.append(point(key))
            key = links[key]
        if len(loop) >= 3:
            loops.append(np.array(loop, float))
    return loops


def _resample(p, step):
    q = np.vstack([p, p[:1]])
    s = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(q, axis=0), axis=1))])
    t = np.linspace(0, s[-1], max(8, int(round(s[-1]/step))), endpoint=False)
    return np.column_stack([np.interp(t, s, q[:, 0]), np.interp(t, s, q[:, 1])])


def _corners(p, k, threshold):
    n = len(p)
    i = np.arange(n)
    d1, d2 = p - p[(i-k) % n], p[(i+k) % n] - p
    turn = np.abs(np.arctan2(d1[:, 0]*d2[:, 1] - d1[:, 1]*d2[:, 0], (d1*d2).sum(1)))
    found = []
    for j in range(n):
        window = [(j+o) % n for o in range(-k, k+1)]
        if turn[j] >= threshold and turn[j] >= turn[window].max() and not (found and found[-1] in window):
            found.append(j)
    if len(found) > 1 and (found[0] - found[-1]) % n <= k:
        found.pop()
    return found


def _unit(v):
    return v / (np.linalg.norm(v) + 1e-12)


def _bezier(c, t):
    t = t[:, None]
    return (1-t)**3*c[0] + 3*(1-t)**2*t*c[1] + 3*(1-t)*t*t*c[2] + t**3*c[3]


def _bezier_with_tangents(pts, u, t1, t2):
    p0, p3 = pts[0], pts[-1]
    a1, a2 = np.outer(3*(1-u)**2*u, t1), np.outer(3*(1-u)*u*u, t2)
    rest = pts - _bezier(np.array([p0, p0, p3, p3]), u)
    c = np.array([[(a1*a1).sum(), (a1*a2).sum()], [(a1*a2).sum(), (a2*a2).sum()]])
    x = np.array([(a1*rest).sum(), (a2*rest).sum()])
    chord = np.linalg.norm(p3 - p0)
    det = c[0, 0]*c[1, 1] - c[0, 1]**2
    l1 = l2 = chord/3
    if abs(det) > 1e-12:
        l1, l2 = (x[0]*c[1, 1] - x[1]*c[0, 1])/det, (c[0, 0]*x[1] - c[0, 1]*x[0])/det
        if min(l1, l2) < 1e-6*chord or max(l1, l2) > 2*chord:
            l1 = l2 = chord/3
    return np.array([p0, p0 + t1*l1, p3 + t2*l2, p3])


def _fit_cubic(pts, t1, t2, tol, depth=0):
    """Schneider's least-squares Bezier fit with Newton reparameterisation."""
    if len(pts) <= 2:
        d = np.linalg.norm(pts[-1] - pts[0])/3
        return [np.array([pts[0], pts[0] + t1*d, pts[-1] + t2*d, pts[-1]])]
    u = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(pts, axis=0), axis=1))])
    u /= u[-1]
    for _ in range(6):
        c = _bezier_with_tangents(pts, u, t1, t2)
        err = np.linalg.norm(_bezier(c, u) - pts, axis=1)
        if err.max() < tol:
            return [c]
        t = u[:, None]
        q1 = 3*((1-t)**2*(c[1]-c[0]) + 2*(1-t)*t*(c[2]-c[1]) + t*t*(c[3]-c[2]))
        q2 = 6*((1-t)*(c[2]-2*c[1]+c[0]) + t*(c[3]-2*c[2]+c[1]))
        d = _bezier(c, u) - pts
        den = (q1*q1).sum(1) + (d*q2).sum(1)
        u = np.clip(u - np.divide((d*q1).sum(1), den, out=np.zeros_like(u), where=np.abs(den) > 1e-12), 0, 1)
        u[0], u[-1] = 0, 1
        u = np.maximum.accumulate(u)
    if depth > 8:
        return [c]
    split = int(np.argmax(err[1:-1])) + 1
    tc = _unit(pts[split-1] - pts[split+1])
    return _fit_cubic(pts[:split+1], t1, tc, tol, depth+1) + _fit_cubic(pts[split:], -tc, t2, tol, depth+1)


def _line(pts):
    centre = pts.mean(0)
    _, _, vt = np.linalg.svd(pts - centre)
    return centre, vt[0], np.abs((pts - centre) @ vt[1])


def _intersect(p, d, q, e):
    m = np.array([d, -e]).T
    if abs(np.linalg.det(m)) < 1e-6:
        return None
    return p + d*np.linalg.solve(m, q - p)[0]


STEP, WINDOW, CORNER, CURVE_TOL, LINE_TOL, SHARPEN = 0.25, 0.9, math.radians(38), 0.25, 0.32, 1.8
SNAP, RUN = math.radians(9), 1.6
SIDE, CAP, PARALLEL, TIP = 3.2, 6.5, math.radians(10), 0.15


def _tangent_angles(p, k):
    n = len(p)
    i = np.arange(n)
    d = p[(i+k) % n] - p[(i-k) % n]
    return np.arctan2(d[:, 1], d[:, 0]) % math.pi


def _axes(loops):
    """The two stroke directions of the face: horizontal and its slant."""
    k = max(2, round(WINDOW/STEP))
    angles = np.concatenate([_tangent_angles(_resample(loop, STEP), k) for loop in loops])
    counts = np.histogram(np.degrees(angles), 180, (0, 180))[0].astype(float)
    counts = sum(np.roll(counts, o) for o in (-2, -1, 0, 1, 2))
    first = int(counts.argmax())
    apart = np.abs((np.arange(180) - first + 90) % 180 - 90) > 25
    second = int((counts*apart).argmax())
    return [math.radians(a + .5) for a in (first, second)]


def _axis_of(angle, axes):
    for axis in axes:
        if abs((angle - axis + math.pi/2) % math.pi - math.pi/2) < SNAP:
            return axis
    return None


def _runs(p, k, axes):
    """Ends of the straight stretches along an axis: a stem joins its curve there."""
    n = len(p)
    on = [_axis_of(a, axes) for a in _tangent_angles(p, k)]
    start = next((i for i in range(n) if on[i] != on[i-1]), None)
    if start is None:
        return []
    cuts, i, seen = [], start, 0
    while seen < n:
        j = 1
        while j < n-seen and on[(i+j) % n] == on[i]:
            j += 1
        if on[i] is not None and j*STEP >= RUN:
            cuts += [i % n, (i+j-1) % n]
        i += j
        seen += j
    return cuts


def _square_ends(pieces, ends, straight, axes):
    """Whatever is short between the two opposite sides of a stroke is its end."""
    count = len(pieces)
    side = [line and len(q)*STEP >= SIDE for q, line in zip(pieces, straight)]
    lines = [i for i in range(count) if side[i]]
    if len(lines) < 2:
        return pieces, ends, straight
    order = [(lines[0]+j) % count for j in range(count)]
    out = ([], [], [])
    at = 0
    while at < count:
        i = order[at]
        at += 1
        out[0].append(pieces[i]), out[1].append(ends[i]), out[2].append(straight[i])
        if not side[i]:
            continue
        look = at
        while not side[order[look % count]]:
            look += 1
        after = order[look % count]
        between = [pieces[order[j % count]] for j in range(at, look)]
        run, back = -ends[i][1][1], ends[after][0][1]
        joint = np.vstack(between) if between else pieces[i][-1:]
        length = np.linalg.norm(np.diff(joint, axis=0), axis=1).sum() if between else 0
        if after == i or length > CAP or run @ back > -math.cos(PARALLEL):
            continue
        far = joint[np.argmax(joint @ run)] + run*TIP
        across = max(axes, key=lambda a: abs(math.cos(a)*run[1] - math.sin(a)*run[0]))
        cut = np.array([math.cos(across), math.sin(across)])
        cut = cut if (ends[after][0][0] - ends[i][0][0]) @ cut > 0 else -cut
        out[0].append(np.array([joint[0], joint[-1]])), out[1].append(((far, cut), (far, -cut))), out[2].append(True)
        at = look
    return out


def _fit_loop(loop, axes=()):
    p = _resample(loop, STEP)
    n = len(p)
    k = max(2, round(WINDOW/STEP))
    cuts = _corners(p, k, CORNER)
    runs = [c for c in _runs(p, k, axes) if all(min((c-o) % n, (o-c) % n) > k for o in cuts)]
    cuts = sorted(set(cuts + runs))
    if not cuts:
        p = np.vstack([p, p[:1]])
        t = _unit(p[1] - p[-2])
        return _fit_cubic(p, t, -t, CURVE_TOL)
    pieces = [p[[(c+j) % n for j in range(((cuts[(i+1) % len(cuts)] - c) % n or n) + 1)]].copy()
              for i, c in enumerate(cuts)]
    ends, straight = [], []
    for q in pieces:
        if len(q) >= 3:
            centre, d, residual = _line(q)
            if residual.max() < LINE_TOL and np.linalg.norm(q[-1] - q[0]) > 0.6:
                axis = _axis_of(math.atan2(d[1], d[0]) % math.pi, axes)
                if axis is not None:
                    d = np.array([math.cos(axis), math.sin(axis)])
                d = d if (q[-1] - q[0]) @ d > 0 else -d
                ends.append(((centre, d), (centre, -d)))
                straight.append(True)
                continue
        m = max(1, min(len(q)//3, k))
        straight.append(False)
        ends.append(((q[0], _unit(q[m] - q[0])), (q[-1], _unit(q[-1-m] - q[-1]))))
    # A stroke ends square. Its two parallel sides meet in one corner or are
    # joined by a short piece: either way that end is a cut along the other
    # axis, just past the point the stroke reaches.
    if len(axes) == 2:
        pieces, ends, straight = _square_ends(pieces, ends, straight, axes)
    # The anti-aliased source cuts sharp corners; restore them where the
    # tangents of both neighbouring pieces meet close to the traced corner.
    for i, a in enumerate(pieces):
        b = pieces[(i+1) % len(pieces)]
        (pa, da), (pb, db) = ends[i][1], ends[(i+1) % len(pieces)][0]
        corner = _intersect(pa, -da, pb, db)
        if corner is not None and np.linalg.norm(corner - a[-1]) < SHARPEN:
            a[-1] = b[0] = corner
    curves = []
    for i, (q, line) in enumerate(zip(pieces, straight)):
        if line:
            curves.append(np.array([q[0], (2*q[0]+q[-1])/3, (q[0]+2*q[-1])/3, q[-1]]))
        else:
            m = max(1, min(len(q)//3, k))
            t1, t2 = _unit(q[m] - q[0]), _unit(q[-1-m] - q[-1])
            # A curve leaves a stem along the stem.
            if straight[i-1] and -ends[i-1][1][1] @ t1 > math.cos(CORNER):
                t1 = -ends[i-1][1][1]
            if straight[(i+1) % len(pieces)] and -ends[(i+1) % len(pieces)][0][1] @ t2 > math.cos(CORNER):
                t2 = -ends[(i+1) % len(pieces)][0][1]
            curves += _fit_cubic(q, t1, t2, CURVE_TOL)
    return curves


def trace_paths(alpha):
    """SVG path data in source pixel units (pixel centres at +0.5)."""
    paths = []
    loops = _contours(np.asarray(alpha, float)/255)
    axes = _axes(loops) if loops else ()
    for loop in loops:
        curves = _fit_loop(loop, axes)
        d = 'M%.3f %.3f' % tuple(curves[0][0] + .5)
        d += ''.join(' C%.3f %.3f %.3f %.3f %.3f %.3f' % tuple((c[1:] + .5).ravel()) for c in curves)
        paths.append(d + 'Z')
    return ' '.join(paths)


def _svg_alpha(path, view, size, stroke=0.0):
    x, y, w, h = view
    outline = f' stroke="#fff" stroke-width="{2*stroke:.4f}"' if stroke else ''
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{size[0]}" height="{size[1]}" viewBox="{x} {y} {w} {h}">'
           f'<path fill="#fff" fill-rule="evenodd"{outline} d="{path}"/></svg>')
    raw = cairosvg.svg2png(bytestring=svg.encode(), output_width=size[0], output_height=size[1])
    return np.asarray(Image.open(io.BytesIO(raw)).convert('RGBA').getchannel('A'), float)/255


# --- Outline sources -----------------------------------------------------------

def _ink(a):
    ys, xs = np.nonzero(a > 1/255)
    return None if len(ys) == 0 else a[ys.min():ys.max()+1, xs.min():xs.max()+1]


class OutlineFont:
    """TrueType outlines; `size` is the em size in source pixels."""
    def __init__(self, path):
        self.path, self.fonts = path, {}
        probe = ImageFont.truetype(str(path), 64)
        self.notdef = bytes(probe.getmask('\U0010fffd'))
        self.probe = probe

    def tile(self, ch, size, scale, stroke):
        if ch.isspace() or bytes(self.probe.getmask(ch)) == self.notdef:
            return None
        px = size*scale
        font = self.fonts.setdefault(px, ImageFont.truetype(str(self.path), px))
        width = round(stroke*scale)
        l, t, r, b = font.getbbox(ch, anchor='ls', stroke_width=width)
        im = Image.new('L', (r-l+4, b-t+4))
        ImageDraw.Draw(im).text((2-l, 2-t), ch, font=font, fill=255, anchor='ls', stroke_width=width, stroke_fill=255)
        return _ink(np.asarray(im, float)/255)


class DonorAtlas:
    """Traced glyphs of a larger atlas; `size` is the donor-to-target pixel ratio."""
    def __init__(self, alpha, glyphs):
        self.path = trace_paths(alpha)
        self.glyphs = {g['ch']: g for g in glyphs}

    def tile(self, ch, size, scale, stroke):
        g = self.glyphs.get(ord(ch))
        if g is None:
            return None
        s = size*scale
        view = (g['x'], g['y'], g['w'], g['h'])
        # stroke is in target source pixels; the SVG units are donor pixels
        return _ink(_svg_alpha(self.path, view, (max(1, round(g['w']*s)), max(1, round(g['h']*s))), stroke/size))


# --- Placement -----------------------------------------------------------------

def _down(a, k):
    h, w = a.shape
    return a.reshape(h//k, k, w//k, k).mean((1, 3))


def _paste(tile, ox, oy, shape):
    canvas = np.zeros(shape)
    th, tw = tile.shape
    y0, x0, y1, x1 = max(0, oy), max(0, ox), min(shape[0], oy+th), min(shape[1], ox+tw)
    if y1 > y0 and x1 > x0:
        canvas[y0:y1, x0:x1] = tile[y0-oy:y1-oy, x0-ox:x1-ox]
    return canvas


def _iou(a, b):
    return float(np.minimum(a, b).sum() / max(1e-6, np.maximum(a, b).sum()))


def fit_glyph(source, glyph, coverage, scale, size, stroke):
    """Best placement of one outline glyph in its cell: (HD cell, IoU, clipped ink share)."""
    src = coverage[glyph['y']:glyph['y']+glyph['h'], glyph['x']:glyph['x']+glyph['w']]
    tile = source.tile(chr(glyph['ch']), size, scale, stroke)
    if tile is None:
        return None, 0.0, 1.0
    shape = (glyph['h']*scale, glyph['w']*scale)
    canvas, iou, clipped = _place(tile, src, scale, shape)
    if clipped > MAX_CLIP:
        # Hinted sources snap ink to whole pixels and the cells are cut tight
        # around it. Narrow such glyphs to their cell like the hinting did,
        # but never deform them by more than MIN_FIT.
        fit = min(1, shape[1]/tile.shape[1]), min(1, shape[0]/tile.shape[0])
        if min(fit) < MIN_FIT:
            return canvas, iou, clipped
        resized = Image.fromarray(tile.astype(np.float32), 'F').resize(
            (max(1, int(tile.shape[1]*fit[0])), max(1, int(tile.shape[0]*fit[1]))), Image.Resampling.LANCZOS)
        canvas, iou, clipped = _place(np.clip(np.asarray(resized, float), 0, 1), src, scale, shape)
    return canvas, iou, clipped


MAX_CLIP, MIN_FIT = 0.02, 0.85


def _place(tile, src, scale, shape):
    total = tile.sum()

    def error(ox, oy):
        canvas = _paste(tile, ox, oy, shape)
        return ((_down(canvas, scale) - src)**2).sum() + (total - canvas.sum())/scale**2
    cx, cy = (shape[1] - tile.shape[1])//2, (shape[0] - tile.shape[0])//2
    step, reach = max(1, scale//2), 2*scale
    best = min((error(cx+dx, cy+dy), cx+dx, cy+dy)
               for dy in range(-reach, reach+1, step) for dx in range(-reach, reach+1, step))
    for _ in range(2):
        _, bx, by = best
        best = min([best] + [(error(bx+dx, by+dy), bx+dx, by+dy)
                             for dy in range(-step, step+1) for dx in range(-step, step+1)])
    canvas = _paste(tile, best[1], best[2], shape)
    return canvas, _iou(_down(canvas, scale), src), 1 - canvas.sum()/max(total, 1e-6)


SAMPLE = 'HOnaegRskdbxSprt'


def sizes_around(estimate, spread=0.08, steps=13):
    return [estimate*(1 - spread + 2*spread*i/(steps-1)) for i in range(steps)]


def cap_height(glyphs):
    return next(g['h'] for g in glyphs if g['ch'] == ord('H'))


def choose(sources, glyphs, coverage, sizes, strokes, scale):
    """Pick the outline source, size and stroke with the best mean IoU on common letters."""
    sample = [g for g in glyphs if chr(g['ch']) in SAMPLE]
    best = None
    for name, source in sources:
        for size in sizes:
            for stroke in strokes:
                score = float(np.mean([fit_glyph(source, g, coverage, scale, size, stroke)[1] for g in sample]))
                if best is None or score > best[0]:
                    best = (score, name, source, size, stroke)
    return best


def upscale(alpha, glyphs, scale, sources=(), sizes=(), strokes=(0,), min_iou=0.55, trace=True):
    """HD alpha for one atlas plus a per-glyph report. Without trace nothing of the
    atlas itself is kept: each cell is the outline or stays empty."""
    coverage = np.asarray(alpha, float)/255
    if trace:
        hd = _svg_alpha(trace_paths(alpha), (0, 0, alpha.width, alpha.height), (alpha.width*scale, alpha.height*scale))
    else:
        hd = np.zeros((alpha.height*scale, alpha.width*scale))
    report = {'method': 'traced source outlines', 'outline_glyphs': 0, 'traced_glyphs': len(glyphs)}
    if not sources:
        return hd, report
    score, name, source, size, stroke = choose(sources, glyphs, coverage, sizes, strokes, scale)
    traced = []
    for g in glyphs:
        canvas, iou, clipped = fit_glyph(source, g, coverage, scale, size, stroke)
        if canvas is None or (trace and (iou < min_iou or clipped > MAX_CLIP)):
            traced.append(g['ch'])
            continue
        hd[g['y']*scale:(g['y']+g['h'])*scale, g['x']*scale:(g['x']+g['w'])*scale] = canvas
    report.update(method=f'{name} outlines', size=round(size, 3), stroke=stroke, sample_iou=round(score, 3),
                  outline_glyphs=len(glyphs) - len(traced), traced_glyphs=len(traced),
                  traced=''.join(chr(c) for c in traced))
    return hd, report
