"""Conduit redrawn from its strokes.

Conduit is a monoline face. Its atlases are 14 to 38 px high, too small for a
trace to recover square ends, so each letter here is a centre line in upright
space, stroked at one width and sheared to the slant. A stroke that runs past
the cap or base line is cut there, which leaves the flat ends of the face.
Width comes from the glyph's FNTX cell; cells, offsets and advances stay.
"""
import copy
import io
import math

import numpy as np
from PIL import Image

import font_hd

SMOOTH = 0.36       # corner radius, as a share of the letter width
# The best fit to a blurred atlas comes out too thin, more so the smaller the
# strokes; the bold 38 px face needs none.
WEIGHT = {'CONDUITMDITC_TT14I': 1.1, 'CONDUITMDITC_TT21I': 1.2, 'FONT_CONDUITMDITCTT38BI': 1}


class Font:
    """Heights in source pixels with y growing downwards and 0 at the cap line."""
    def __init__(self, cells, sw, slant):
        self.sw, self.slant = sw, slant
        cap, x, d, p = (cells[ord(c)] for c in 'Hndp')
        self.cap = cap['h'] - .1
        self.base_row = cap['yoff'] + cap['h']
        self.x_top = self.cap - (x['h'] - .1)
        self.asc = self.cap - (self.base_row - d['yoff'] - .2)
        self.desc = self.cap + (p['yoff'] + p['h'] - self.base_row) - .2


class Box:
    """Centre lines of one letter of upright width w."""
    def __init__(self, font, w):
        s = font.sw
        self.font, self.w, self.sw = font, w, s
        self.l, self.r, self.c = s/2, w - s/2, w/2
        self.t, self.b = s/2, font.cap - s/2
        self.m = font.cap/2
        self.xt = font.x_top + s/2
        self.xm = (self.xt + self.b)/2
        self.at = font.asc + s/2
        self.dt = font.desc - s/2
        self.top, self.base = 0, font.cap
        self.e = 3*s
        self.R = min(SMOOTH*w, font.cap*.27)

    def apex(self, half):
        """Centre line height of a point whose two strokes fall half across per
        cap height: the outer tip is cut narrow on the cap line, not wide."""
        return .55*self.sw*math.hypot(half, self.font.cap)/self.font.cap - self.sw*.05


def _unit(a, b):
    d = (b[0]-a[0], b[1]-a[1])
    n = math.hypot(*d) or 1
    return d[0]/n, d[1]/n


def path(points, closed=False):
    """Polyline through (x, y) or (x, y, radius); a radius rounds that vertex."""
    pts = [(p[0], p[1], p[2] if len(p) > 2 else 0) for p in points]
    n = len(pts)
    out = []
    for i, (x, y, r) in enumerate(pts):
        edge = not closed and i in (0, n-1)
        if edge or not r:
            out.append(('L', x, y))
            continue
        a, b = pts[(i-1) % n], pts[(i+1) % n]
        la, lb = math.hypot(a[0]-x, a[1]-y), math.hypot(b[0]-x, b[1]-y)
        r = min(r, la*.5 if (a[2] or closed) else la*.95, lb*.5 if (b[2] or closed) else lb*.95)
        ua, ub = _unit((x, y), a), _unit((x, y), b)
        out.append(('L', x+ua[0]*r, y+ua[1]*r))
        out.append(('Q', x, y, x+ub[0]*r, y+ub[1]*r))
    d = 'M%.3f %.3f' % out[0][1:3]
    for op in out[1:]:
        d += ('L%.3f %.3f' if op[0] == 'L' else 'Q%.3f %.3f %.3f %.3f') % op[1:]
    return d + ('Z' if closed else '')


def rect(b, l, t, r, bot, radius=None):
    """A closed rounded box, the bowl of the face."""
    q = b.R if radius is None else radius
    return ('closed', [(l, t, q), (r, t, q), (r, bot, q), (l, bot, q)])


def render(strokes, box, scale):
    """Alpha of the sheared letter from the band top to its bottom, with the
    column of the left edge of the ink."""
    font = box.font
    style = f'fill="none" stroke="#fff" stroke-width="{font.sw}" stroke-linejoin="miter" stroke-miterlimit="3.4" stroke-linecap="butt"'
    defs, paths = '', ''
    for i, s in enumerate(strokes):
        clip = ''
        if len(s) > 2:
            defs += f'<clipPath id="s{i}"><rect x="-99" y="{s[2][0]}" width="999" height="{s[2][1]-s[2][0]}"/></clipPath>'
            clip = f' clip-path="url(#s{i})"'
        paths += f'<g{clip}><path d="{path(s[1], s[0] == "closed")}" {style}/></g>'
    top, bottom = box.top, box.base
    pad = font.cap
    width = box.w + 2*pad + (bottom - top)*font.slant
    height = bottom - top
    px = (round(width*scale), round(height*scale))
    shear = font.slant*font.cap + pad
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{px[0]}" height="{px[1]}" '
           f'viewBox="0 {top} {width} {height}"><defs><clipPath id="c">'
           f'<rect x="-99" y="{top}" width="999" height="{height}"/></clipPath>{defs}</defs>'
           f'<g clip-path="url(#c)"><g transform="matrix(1 0 {-font.slant} 1 {shear} 0)">{paths}</g></g></svg>')
    raw = font_hd.cairosvg.svg2png(bytestring=svg.encode(), output_width=px[0], output_height=px[1])
    a = np.asarray(Image.open(io.BytesIO(raw)).convert('RGBA').getchannel('A'), float)/255
    columns = np.nonzero(a.max(0) > 1/255)[0]
    if not len(columns):
        return None
    return a[:, columns[0]:columns[-1]+1]


def upright(cell, font, top, bottom, share):
    """A stem adds the slant across the height to the ink. A letter on a point or
    two feet does not: share says how much of the slant counts."""
    return max(font.sw*1.2, cell['w'] - share*font.slant*(bottom - top))


def draw_in_cell(spec, cell, font, scale):
    """HD alpha of a sign that fills its cell."""
    inner = copy.copy(font)
    inner.sw = font.sw*spec.weight
    inner.cap, inner.x_top, inner.asc, inner.desc = cell['h'], 0, 0, cell['h']
    box = Box(inner, max(inner.sw*1.05, cell['w'] - spec.share*font.slant*cell['h']))
    canvas = np.zeros((cell['h']*scale, cell['w']*scale))
    tile = render(spec(box), box, scale)
    if tile is None:
        return canvas
    col = (canvas.shape[1] - tile.shape[1])//2
    x0, x1 = max(0, col), min(canvas.shape[1], col + tile.shape[1])
    y1 = min(canvas.shape[0], tile.shape[0])
    canvas[:y1, x0:x1] = tile[:y1, x0-col:x1-col]
    return canvas


def draw(spec, cell, font, scale):
    """HD alpha of the whole cell."""
    if getattr(spec, 'in_cell', False):
        return draw_in_cell(spec, cell, font, scale)
    probe = Box(font, 10)
    probe_strokes = spec(probe)
    w = cell['w']
    for _ in range(3):
        w = upright(cell, font, probe.top, probe.base, getattr(spec, 'share', 1))
        probe = Box(font, w)
        probe_strokes = spec(probe)
    tile = render(probe_strokes, probe, scale)
    canvas = np.zeros((cell['h']*scale, cell['w']*scale))
    if tile is None:
        return canvas
    row = round(((font.base_row - cell['yoff']) - font.cap + probe.top)*scale)
    col = (canvas.shape[1] - tile.shape[1])//2
    y0, y1 = max(0, row), min(canvas.shape[0], row + tile.shape[0])
    x0, x1 = max(0, col), min(canvas.shape[1], col + tile.shape[1])
    if y1 > y0 and x1 > x0:
        canvas[y0:y1, x0:x1] = tile[y0-row:y1-row, x0-col:x1-col]
    return canvas


def score(canvas, coverage, cell, scale):
    source = coverage[cell['y']:cell['y']+cell['h'], cell['x']:cell['x']+cell['w']]
    return font_hd._iou(font_hd._down(canvas, scale), source)


def fit_width(cells, coverage, slant, scale, table, sample):
    """The stroke width that matches the source best."""
    probe = max(cells[ord('H')]['h']*.1, 1)
    best = None
    for k in np.arange(.8, 2.2, .1):
        font = Font(cells, float(probe*k), slant)
        value = float(np.mean([score(draw(table[c], cells[ord(c)], font, scale), coverage, cells[ord(c)], scale)
                               for c in sample]))
        if best is None or value > best[0]:
            best = (value, font.sw)
    return best[1]


def in_cell(fn=None, weight=.85, share=1):
    """A sign drawn in the cell's own box, at weight of the letter stroke."""
    def mark(fn):
        fn.in_cell, fn.weight, fn.share = True, weight, share
        return fn
    return mark(fn) if fn else mark


# --- Latin and digits -----------------------------------------------------------

def open_(*points):
    return ('open', list(points))


def within(stroke, top, bottom):
    """A stroke cut flat at its own lines, like an arm at the x-height."""
    return (*stroke[:2], (top, bottom))


def full(fn):
    fn.share = 0
    return fn


def half(fn):
    fn.share = .5
    return fn


def _A(b):
    h = b.apex(b.c)
    e = b.e
    run = (b.c - b.l)/(b.font.cap)
    return [open_((b.c - run*(b.base+e - h), b.base+e), (b.c, h), (b.c + run*(b.base+e - h), b.base+e)),
            open_((b.c - run*(b.base*.72 - h), b.base*.72), (b.c + run*(b.base*.72 - h), b.base*.72))]


def _bowls(b, split=None, shrink=.6):
    m = split or b.m
    R = min(b.R, (m - b.t)*.9)
    return [open_((b.r - shrink, m), (b.r - shrink, b.t, R*.8), (b.l, b.t), (b.l, b.b), (b.r, b.b, R), (b.r, m, R), (b.l, m))]


def _C(b, top=None):
    y = b.t if top is None else top
    q = min(b.R*1.1, (b.b - y)*.5)
    d = q*.72
    return [open_((b.r, y + d), (b.r, y, q), (b.l, y, q), (b.l, b.b, q), (b.r, b.b, q), (b.r, b.b - d))]


def _G(b):
    q = b.R*1.1
    return [open_((b.r, b.t + q*.72), (b.r, b.t, q), (b.l, b.t, q), (b.l, b.b, q), (b.r, b.b, q), (b.r, b.m + b.sw*.3), (b.c, b.m + b.sw*.3))]


def _J(b):
    return [open_((b.r, -b.e), (b.r, b.b, b.R), (b.l, b.b, b.R), (b.l, b.b - b.R*.9))]


def _M(b):
    return [open_((b.l, -b.e), (b.l, b.base+b.e)), open_((b.r, -b.e), (b.r, b.base+b.e)),
            open_((b.l, 0), (b.c, b.base*.68), (b.r, 0))]


def _N(b):
    return [open_((b.l, b.base+b.e), (b.l, b.t*.4), (b.r, b.b + b.t*.6), (b.r, -b.e))]


def _Q(b):
    b.base = b.font.cap + 1
    b.b = b.font.cap - b.sw/2
    return [rect(b, b.l, b.t, b.r, b.b), open_((b.c + b.sw*.5, b.b - b.sw*1.5), (b.r + b.sw*.5, b.base + b.sw))]


def _S(b, top=None, bottom=None):
    t, bot = (b.t if top is None else top), (b.b if bottom is None else bottom)
    m = (t + bot)/2
    q = (bot - t)*.27
    d = q*.6
    return [open_((b.r, t + d), (b.r, t, q), (b.l, t, q), (b.l, m, q), (b.r, m, q), (b.r, bot, q), (b.l, bot, q), (b.l, bot - d))]


def _U(b):
    return [open_((b.l, -b.e), (b.l, b.b, b.R), (b.r, b.b, b.R), (b.r, -b.e))]


def _V(b):
    h = b.base - b.apex(b.c)
    run = (b.c - b.l)/b.font.cap
    return [open_((b.c - run*(h + b.e), -b.e), (b.c, h), (b.c + run*(h + b.e), -b.e))]


def _W(b):
    h = b.base - b.apex(b.w/4)
    q = b.w/4
    run = (q - b.l*.6)/b.font.cap
    return [open_((q - run*(h+b.e), -b.e), (q, h), (b.c, b.t*.8 + b.font.cap*.12), (b.w - q, h), (b.w - q + run*(h+b.e), -b.e))]


def _X(b):
    e = b.e*(b.r - b.l)/b.font.cap
    return [open_((b.l*.7 - e, -b.e), (b.w - b.l*.7 + e, b.base + b.e)),
            open_((b.w - b.l*.7 + e, -b.e), (b.l*.7 - e, b.base + b.e))]


def _Y(b):
    v = b.m + b.sw*.4
    run = (b.c - b.l*.7)/v
    return [open_((b.c - run*(v + b.e), -b.e), (b.c, v), (b.c + run*(v + b.e), -b.e)),
            open_((b.c, v), (b.c, b.base + b.e))]


def _bars(*spans):
    return list(spans)


CAPS = {
    'A': full(_A),
    'B': lambda b: [open_((b.l, b.m), (b.r - .7, b.m, min(b.R, b.m*.8)), (b.r - .7, b.t, min(b.R, b.m*.8)), (b.l, b.t), (b.l, b.b), (b.r, b.b, min(b.R, b.m*.8)), (b.r, b.m, min(b.R, b.m*.8)), (b.l, b.m))],
    'C': _C,
    'D': lambda b: [('closed', [(b.l, b.t), (b.r, b.t, b.R), (b.r, b.b, b.R), (b.l, b.b)])],
    'E': lambda b: [open_((b.r, b.t), (b.l, b.t), (b.l, b.b), (b.r, b.b)), open_((b.l, b.m), (b.r - b.sw*.5, b.m))],
    'F': lambda b: [open_((b.r, b.t), (b.l, b.t), (b.l, b.base + b.e)), open_((b.l, b.m), (b.r - b.sw*.7, b.m))],
    'G': _G,
    'H': lambda b: [open_((b.l, -b.e), (b.l, b.base + b.e)), open_((b.r, -b.e), (b.r, b.base + b.e)), open_((b.l, b.m), (b.r, b.m))],
    'I': lambda b: [open_((b.l*.3, b.t), (b.r + b.l*.7, b.t)), open_((b.l*.3, b.b), (b.r + b.l*.7, b.b)), open_((b.c, b.t), (b.c, b.b))],
    'J': _J,
    'K': lambda b: [open_((b.l, -b.e), (b.l, b.base + b.e)), open_((b.l, b.base*.6), (b.r + b.sw*.9, -b.e)), open_((b.c*.72, b.base*.42), (b.r + b.sw*.55, b.base + b.e))],
    'L': lambda b: [open_((b.l, -b.e), (b.l, b.b), (b.w, b.b))],
    'M': _M,
    'N': _N,
    'O': lambda b: [rect(b, b.l, b.t, b.r, b.b)],
    'P': lambda b: [open_((b.l, b.base + b.e), (b.l, b.t), (b.r, b.t, b.R*.9), (b.r, b.m + b.sw*.5, b.R*.9), (b.l, b.m + b.sw*.5))],
    'Q': _Q,
    'R': lambda b: [open_((b.l, b.base + b.e), (b.l, b.t), (b.r, b.t, b.R*.9), (b.r, b.m + b.sw*.5, b.R*.9), (b.l, b.m + b.sw*.5)),
                    open_((b.c, b.m + b.sw*.5), (b.r + b.sw*.45, b.base + b.e))],
    'S': _S,
    'T': lambda b: [open_((0, b.t), (b.w, b.t)), open_((b.c, b.t), (b.c, b.base + b.e))],
    'U': _U,
    'V': full(_V),
    'W': full(_W),
    'X': _X,
    'Y': full(_Y),
    'Z': lambda b: [open_((b.l*.6, b.t), (b.r, b.t), (b.l, b.b), (b.w, b.b))],
}


def lower(fn):
    """Lets a lower-case letter use a band of its own."""
    return fn


def _x(b):
    """Band of the x-height."""
    return b.xt, b.b


def _o(b):
    return [rect(b, b.l, b.xt, b.r, b.b, min(b.R, (b.b - b.xt)*.46))]


def _bowl_right(b, stem_top, stem_bottom):
    q = min(b.R, (b.b - b.xt)*.46)
    return [open_((b.l, stem_top), (b.l, stem_bottom)), rect(b, b.l, b.xt, b.r, b.b, q)]


def _bowl_left(b, stem_top, stem_bottom):
    q = min(b.R, (b.b - b.xt)*.46)
    return [open_((b.r, stem_top), (b.r, stem_bottom)), rect(b, b.l, b.xt, b.r, b.b, q)]


def _arch(b, l, r, q):
    return [open_((l, b.base + b.e), (l, b.xt, q), (r, b.xt, q), (r, b.base + b.e))]


def _a(b):
    q = min(b.R, (b.b - b.xt)*.4)
    m = (b.xt + b.b)/2 + b.sw*.1
    return [open_((b.l + .4, b.xt + q*.5), (b.l + .4, b.xt, q*.7), (b.r, b.xt, q), (b.r, b.base + b.e)),
            open_((b.r, m), (b.l, m, q*.8), (b.l, b.b, q*.8), (b.r, b.b))]


def _e(b):
    q = min(b.R, (b.b - b.xt)*.46)
    m = (b.xt + b.b)/2
    return [open_((b.l, m), (b.r, m), (b.r, b.xt, q), (b.l, b.xt, q), (b.l, b.b, q), (b.r, b.b, q*.8), (b.r, b.b - q*.45))]


def _c(b):
    return _C(b, b.xt)


def _f(b):
    x = b.c - b.sw*.2
    return [open_((b.w, b.at), (x, b.at, b.sw*1.4), (x, b.base + b.e)), open_((0, b.xt), (b.r, b.xt))]


def _g(b):
    q = min(b.R, (b.b - b.xt)*.38)
    mid = b.b - .5*(b.b - b.xt)*.25
    return [rect(b, b.l + .4, b.xt, b.r - .4, mid, q), open_((b.r - .4, b.xt), (b.w + .3, b.xt)),
            open_((b.c, mid), (b.r, mid + (b.dt - mid)*.5, q), (b.r, b.dt, q), (b.l, b.dt, q), (b.l, b.dt - q*.7))]


def _i(b):
    return [open_((b.c, b.xt - b.sw*.5), (b.c, b.base + b.e)), open_((b.c, b.font.asc), (b.c, b.font.asc + b.sw*1.15))]


def _j(b):
    return [open_((b.r, b.xt - b.sw*.5), (b.r, b.dt, b.sw*1.3), (b.l*.3, b.dt)), open_((b.r, b.font.asc), (b.r, b.font.asc + b.sw*1.15))]


def _k(b):
    top = b.font.x_top
    return [open_((b.l, b.at - b.e), (b.l, b.base + b.e)),
            within(open_((b.l, b.b - (b.b - top)*.28), (b.r + b.sw*.9, top - b.sw)), top, b.base),
            open_((b.c*.75, b.xt + (b.b - b.xt)*.42), (b.r + b.sw*.4, b.base + b.e))]


def _l(b):
    return [open_((b.l, b.at - b.e), (b.l, b.b, b.sw*1.2), (b.w, b.b))]


def _m(b):
    q = min(b.w*.16, (b.b - b.xt)*.46)
    mid = b.c
    return _arch(b, b.l, mid, q) + _arch(b, mid, b.r, q)


def _p(b):
    return _bowl_right(b, b.xt, b.dt + b.e)


def _q(b):
    return _bowl_left(b, b.xt, b.dt + b.e)


def _r(b):
    q = min(b.R, (b.b - b.xt)*.5)
    return [open_((b.l, b.base + b.e), (b.l, b.xt, q), (b.r, b.xt, q*.4), (b.r + .1, b.xt + b.sw*.45))]


def _s(b):
    return _S(b, b.xt, b.b)


def _t(b):
    x = b.c - b.sw*.15
    return [open_((x, b.font.asc + b.sw*.8), (x, b.b, b.sw*1.2), (b.r, b.b)), open_((0, b.xt), (b.w, b.xt))]


def _u(b):
    q = min(b.R, (b.b - b.xt)*.5)
    return [open_((b.l, b.xt - b.sw), (b.l, b.b, q), (b.r, b.b, q*.6)), open_((b.r, b.xt - b.sw), (b.r, b.base + b.e))]


def _v(b):
    h = b.base - b.apex(b.c)
    run = (b.c - b.l)/(b.base - b.xt)
    return [open_((b.c - run*(h - b.xt + b.sw*1.5), b.xt - b.sw*1.5), (b.c, h), (b.c + run*(h - b.xt + b.sw*1.5), b.xt - b.sw*1.5))]


def _w(b):
    h = b.base - b.apex(b.w/4)
    q = b.w/4
    top = b.xt - b.sw*1.5
    run = (q - b.l*.6)/(h - b.xt)
    return [open_((q - run*(h - top), top), (q, h), (b.c, b.xt + (b.b - b.xt)*.15), (b.w - q, h), (b.w - q + run*(h - top), top))]


def _x2(b):
    h = b.base - b.font.x_top
    e = b.e*(b.r - b.l)/h
    top, bottom = b.font.x_top - b.e, b.base + b.e
    return [open_((b.l*.7 - e, top), (b.w - b.l*.7 + e, bottom)), open_((b.w - b.l*.7 + e, top), (b.l*.7 - e, bottom))]


def _y(b):
    run = (b.c - b.l)/(b.base - b.xt)
    top = b.xt - b.sw*1.5
    return [open_((b.l - run*(b.xt - top), top), (b.c, b.b + b.sw*.3)),
            open_((b.w - b.l + run*(b.xt - top), top), (b.c, b.b + b.sw*.3), (b.l + .3, b.dt, b.sw), (0, b.dt))]


def _z(b):
    return [open_((b.l*.6, b.xt), (b.r, b.xt), (b.l, b.b), (b.w, b.b))]


def band(fn, set_top, set_base):
    def spec(b):
        if set_top:
            b.top = b.font.asc if set_top == 'asc' else b.font.x_top
        if set_base:
            b.base = b.font.desc
        return fn(b)
    spec.share = getattr(fn, 'share', 1)
    return spec


def tall(fn):
    """A letter that rises to the ascender line."""
    return band(fn, 'asc', False)


def deep(fn):
    """A letter that falls to the descender line."""
    spec = band(fn, None, True)
    return spec


def short(fn):
    return band(fn, 'x', False)


def both(fn):
    return band(fn, 'asc', True)


LOWER = {
    'a': short(_a), 'b': tall(lambda b: _bowl_right(b, b.at - b.e, b.b)), 'c': short(_c),
    'd': tall(lambda b: _bowl_left(b, b.at - b.e, b.base + b.e)), 'e': short(_e), 'f': tall(half(_f)), 'g': deep(short(_g)),
    'h': tall(lambda b: [open_((b.l, b.at - b.e), (b.l, b.base + b.e))] + _arch(b, b.l, b.r, min(b.R, (b.b - b.xt)*.5))),
    'i': tall(_i), 'j': both(_j), 'k': tall(_k), 'l': tall(_l), 'm': short(_m),
    'n': short(lambda b: _arch(b, b.l, b.r, min(b.R, (b.b - b.xt)*.5))),
    'o': short(_o), 'p': deep(short(_p)), 'q': deep(short(_q)), 'r': short(_r), 's': short(_s),
    't': tall(half(_t)), 'u': short(_u), 'v': short(full(_v)), 'w': short(full(_w)), 'x': short(_x2), 'y': deep(short(full(_y))), 'z': short(_z),
}


def _four(b):
    x = b.r - b.sw*.5
    y = b.b - b.font.cap*.2
    return [open_((x, -b.e), (x, b.base + b.e)), open_((b.w, y), (b.l*.5, y), (x, b.t*.2))]


def _seven(b):
    return [open_((b.l*.2, b.t*2.2), (b.l*.2, b.t), (b.r, b.t), (b.l + b.sw*.8, b.base + b.e))]


DIGITS = {
    '0': lambda b: [rect(b, b.l, b.t, b.r, b.b)],
    '1': lambda b: [open_((b.c - b.sw*1.5, b.t), (b.c + b.sw*.3, b.t), (b.c + b.sw*.3, b.base + b.e))],
    '2': lambda b: [open_((b.l, b.t + b.R*.55), (b.l, b.t, b.R), (b.r, b.t, b.R), (b.r, b.m - b.sw*.5, b.R), (b.l, b.b), (b.w, b.b))],
    '3': lambda b: [open_((b.l*.6, b.t), (b.r, b.t), (b.c, b.m - b.sw*.3), (b.r, b.m - b.sw*.3, b.R*.85), (b.r, b.b, b.R), (b.l, b.b, b.R), (b.l, b.b - b.R*.6))],
    '4': _four,
    '5': lambda b: [open_((b.r, b.t), (b.l + .5, b.t), (b.l, b.m - b.sw*.5), (b.r, b.m - b.sw*.5, b.R*.85), (b.r, b.b, b.R), (b.l, b.b, b.R), (b.l, b.b - b.R*.6))],
    '6': lambda b: [open_((b.r - .8, -b.e*.2), (b.l, b.m + b.sw*.3, b.R)), rect(b, b.l, b.m - b.sw*.3, b.r, b.b, b.R*.85)],
    '7': _seven,
    '8': lambda b: [rect(b, b.l + .5, b.t, b.r - .5, b.m, b.R*.65), rect(b, b.l, b.m, b.r, b.b, b.R*.75)],
    '9': lambda b: [open_((b.l + .8, b.base + b.e*.2), (b.r, b.m - b.sw*.3, b.R)), rect(b, b.l, b.t, b.r, b.m + b.sw*.3, b.R*.85)],
}

# In the 38 px face the figures are old style, of three heights, so each fills its cell.
LATIN = {**CAPS, **LOWER, **{c: in_cell(fn, 1) for c, fn in DIGITS.items()}}


# --- Signs ----------------------------------------------------------------------
# With these an atlas needs nothing from the disc's own glyphs. A sign has no common
# band, so it fills its FNTX cell, which fixes its size and height. Only what English
# text can show is drawn, plus the accents of the names in it.

def _bar_(b, y, l=None, r=None):
    return open_((0 if l is None else l, y), (b.w if r is None else r, y))


def _stick(b, x, top, bottom):
    return open_((x, top), (x, bottom))


def _dots(b, *ys):
    d = b.sw*1.15
    return [_stick(b, b.c, y, y + d) for y in ys]


def _tail(b, top):
    x = b.r
    return open_((x, top), (x, top + (b.base - top)*.5), (b.l, b.base + b.sw))


def _exclaim(b):
    return [_stick(b, b.c, -b.sw, b.base - b.sw*1.9), *_dots(b, b.base - b.sw*1.15)]


def _question(b):
    y = b.m + b.sw*.3
    R = b.w*.3
    return [open_((b.l, b.t + R), (b.l, b.t, R), (b.r, b.t, R), (b.r, y, R), (b.c, y), (b.c, b.base - b.sw*1.9)), *_dots(b, b.base - b.sw*1.15)]


def _star(b):
    r = b.base*.5
    y = b.m
    return [open_((b.c + r*math.sin(a), y - r*math.cos(a)), (b.c - r*math.sin(a), y + r*math.cos(a))) for a in (0, math.pi/3, 2*math.pi/3)]


def _paren(b, a, c):
    q = b.base*.28
    return [open_((c, -b.sw*.2), (a, b.base*.2, q), (a, b.base*.8, q), (c, b.base + b.sw*.2))]


def _bracket(b, a, c):
    return [open_((c, b.t), (a, b.t), (a, b.b), (c, b.b))]


def _brace(b, a, c):
    n = b.c
    return [open_((c, b.t), (n, b.t, b.sw), (n, b.m - b.sw*.7, b.sw*.6), (a, b.m), (n, b.m + b.sw*.7, b.sw*.6), (n, b.b, b.sw), (c, b.b))]


def _percent(b):
    w = b.w*.36
    h = b.base*.42
    q = w*.32
    return [rect(b, b.sw/2, b.sw/2, b.sw/2 + w, h, q), rect(b, b.w - b.sw/2 - w, b.base - h, b.w - b.sw/2, b.base - b.sw/2, q),
            open_((b.w*.26, b.base + b.sw), (b.w*.74, -b.sw))]


def _amp(b):
    q = b.w*.2
    return [open_((b.w, b.base + b.sw*.2), (b.w*.22, b.base*.26, q), (b.w*.3, b.t, q), (b.w*.68, b.t, q), (b.w*.7, b.base*.34, q),
                  (b.l, b.base*.6, q), (b.l, b.b, q), (b.w*.62, b.b, q), (b.w*.9, b.m))]


def _at(b):
    q = b.R
    i, k = b.base*.32, b.base*.68
    x, y = b.w*.34, b.w*.66
    return [open_((b.w*.8, b.b), (b.l, b.b, q), (b.l, b.t, q), (b.r, b.t, q), (b.r, k, q*.5), (y, k), (y, i), (x, i, q*.4), (x, k, q*.4), (y, k))]


def _hash(b):
    a, c = b.base*.32, b.base*.68
    return [_stick(b, b.w*.32, -b.sw, b.base + b.sw), _stick(b, b.w*.68, -b.sw, b.base + b.sw), _bar_(b, a), _bar_(b, c)]


def _ring(b):
    return [rect(b, b.l, b.t, b.r, b.b, b.R)]


def _copyright(b):
    i = min(b.sw*2.7, b.w*.3)
    q = b.R*.45
    return _ring(b) + [open_((b.r - i, b.t + i), (b.l + i, b.t + i, q), (b.l + i, b.b - i, q), (b.r - i, b.b - i))]


def _registered(b):
    i = min(b.sw*2.7, b.w*.3)
    y = b.m + b.sw*.15
    return _ring(b) + [open_((b.l + i, b.b - i), (b.l + i, b.t + i), (b.r - i, b.t + i, b.R*.3), (b.r - i, y, b.R*.3), (b.l + i, y)),
                       open_((b.c, y), (b.r - i, b.b - i))]


def _trademark(b):
    x = b.w*.42
    h = b.base
    return [_bar_(b, b.t, 0, x), _stick(b, x*.5, b.t, h), open_((x + b.sw, h + b.sw), (x + b.sw, -b.sw*.2), (b.w*.71, h*.8), (b.r, -b.sw*.2), (b.r, h + b.sw))]


def _accent(mark):
    def wrap(fn):
        def spec(b):
            strokes = fn(b)
            b.top = b.font.asc - b.sw
            return strokes + mark(b, b.xt - b.sw*1.3)
        spec.share = getattr(fn, 'share', 1)
        return spec
    return wrap


def _acute(b, y):
    return [open_((b.c - b.sw*.3, y), (b.c + b.sw*.9, y - b.sw*1.6))]


def _dieresis(b, y):
    d = b.sw*1.15
    return [open_((b.c - b.sw*.95, y - d), (b.c - b.sw*.95, y)), open_((b.c + b.sw*.95, y - d), (b.c + b.sw*.95, y))]


def _small_tilde(b, y):
    q = b.sw*.8
    y -= b.sw*.6
    half_height = b.sw*.35
    return [open_((b.l*.5, y + half_height), (b.w*.3, y - half_height, q), (b.w*.7, y + half_height, q), (b.w - b.l*.5, y - half_height))]


def _i_body(b):
    return [open_((b.c, b.xt - b.sw*.5), (b.c, b.base + b.e))]


def _pound(b):
    y = b.m + b.sw*.4
    return [open_((b.r, b.t + b.R*.8), (b.r, b.t, b.R*.8), (b.c*.9, b.t, b.R*.8), (b.c*.9, b.b), (b.l*.4, b.b), (b.w, b.b)), open_((b.l*.5, y), (b.r - b.sw*.5, y))]


SIGNS = {
    '.': in_cell(lambda b: [_stick(b, b.c, -b.sw, b.base + b.sw)], 1, 0),
    ',': in_cell(lambda b: [_tail(b, -b.sw)], 1, .5),
    ':': in_cell(lambda b: _dots(b, 0, b.base - b.sw*1.15), 1, 0),
    ';': in_cell(lambda b: [*_dots(b, 0), _tail(b, b.base - b.sw*2.4)], 1, .5),
    '!': in_cell(_exclaim, 1, 0),
    '?': in_cell(_question),
    "'": in_cell(lambda b: [_stick(b, b.c, -b.sw, b.base + b.sw)], 1, 0),
    '`': in_cell(lambda b: [open_((0, -b.sw), (b.w, b.base + b.sw))], .8, -1),
    '"': in_cell(lambda b: [_stick(b, b.l, -b.sw, b.base + b.sw), _stick(b, b.r, -b.sw, b.base + b.sw)], .85, 1),
    '-': in_cell(lambda b: [_bar_(b, b.m)], 1, 0),
    '_': in_cell(lambda b: [_bar_(b, b.m)], 1, 0),
    '=': in_cell(lambda b: [_bar_(b, b.t), _bar_(b, b.b)], .9, 0),
    '+': in_cell(lambda b: [_bar_(b, b.m), _stick(b, b.c, -b.sw, b.base + b.sw)], .9, .5),
    '*': in_cell(_star, .7, 0),
    '#': in_cell(_hash, .75),
    '$': in_cell(lambda b: _S(b, b.t + b.sw*.8, b.b - b.sw*.8) + [_stick(b, b.c, -b.sw, b.base + b.sw)], .75),
    '%': in_cell(_percent, .65),
    '&': in_cell(_amp, .85),
    '@': in_cell(_at, .6),
    '/': in_cell(lambda b: [open_((b.l, b.base + b.sw), (b.r, -b.sw))], .85),
    '\\': in_cell(lambda b: [open_((b.l, -b.sw), (b.r, b.base + b.sw))], .85, -1),
    '(': in_cell(lambda b: _paren(b, b.l, b.r)),
    ')': in_cell(lambda b: _paren(b, b.r, b.l)),
    '[': in_cell(lambda b: _bracket(b, b.l, b.r)),
    ']': in_cell(lambda b: _bracket(b, b.r, b.l)),
    '{': in_cell(lambda b: _brace(b, b.l, b.r), .75),
    '}': in_cell(lambda b: _brace(b, b.r, b.l), .75),
    '<': in_cell(lambda b: [open_((b.w, -b.sw*.6), (b.sw*.9, b.m), (b.w, b.base + b.sw*.6))], .9, .5),
    '>': in_cell(lambda b: [open_((0, -b.sw*.6), (b.w - b.sw*.9, b.m), (0, b.base + b.sw*.6))], .9, .5),
    '\xa9': in_cell(_copyright, .55),
    '\xae': in_cell(_registered, .55),
    '\xb0': in_cell(lambda b: [rect(b, b.l, b.t, b.r, b.b, b.w*.3)], .6, 0),
    '™': in_cell(_trademark, .55, 0),
    '…': in_cell(lambda b: [_stick(b, x, -b.sw, b.base + b.sw) for x in (b.sw*.5, b.c, b.w - b.sw*.5)], 1, 0),
    '\xa3': _pound,
    '\xe1': _accent(_acute)(_a),
    '\xe9': _accent(_acute)(_e),
    '\xed': _accent(_acute)(_i_body),
    '\xf1': _accent(_small_tilde)(lambda b: _arch(b, b.l, b.r, min(b.R, (b.b - b.xt)*.5))),
    '\xf3': _accent(_acute)(_o),
    '\xf6': _accent(_dieresis)(_o),
    '\xfa': _accent(_acute)(_u),
    '\xfc': _accent(_dieresis)(_u),
}


def redraw(hd, name, cells, coverage, table, scale, sample='HENTIMLUOD'):
    """Draws table's characters over the HD alpha; returns the font and the IoUs."""
    k = max(2, round(font_hd.WINDOW/font_hd.STEP))
    axes = font_hd._axes(font_hd._contours(coverage))
    slant = min(math.tan(abs(a - math.pi/2)) for a in axes)
    keyed = {c: table[c] for c in table if ord(c) in cells}
    sw = WEIGHT[name]*fit_width(cells, coverage, slant, scale, keyed, [c for c in sample if c in keyed])
    font = Font(cells, sw, slant)
    scores = {}
    for c, spec in keyed.items():
        g = cells[ord(c)]
        if not g['w'] or not g['h']:
            continue
        canvas = draw(spec, g, font, scale)
        hd[g['y']*scale:(g['y']+g['h'])*scale, g['x']*scale:(g['x']+g['w'])*scale] = canvas
        scores[c] = round(score(canvas, coverage, g, scale), 2)
    return font, scores
