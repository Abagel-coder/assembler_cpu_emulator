#!/usr/bin/env python3
"""Render results/*.csv into committed SVG charts under docs/ (stdlib only)."""
import csv, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "results")
DOCS = os.path.join(ROOT, "docs")
PALETTE = ["#5b8def", "#9b59b6", "#2ecc71", "#e67e22", "#e74c3c", "#16a085"]


def read_csv(name):
    with open(os.path.join(RES, name)) as f:
        return list(csv.DictReader(f))


def svg_header(w, h):
    return [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" '
            f'font-family="system-ui,Arial,sans-serif">',
            f'<rect width="{w}" height="{h}" fill="#ffffff"/>']


def grouped_bars(rows, groups, series, value_key, title, ymax, ylabel, fname):
    W, H = 760, 430
    L, R, T, B = 60, 20, 50, 90
    pw, ph = W - L - R, H - T - B
    s = svg_header(W, H)
    s.append(f'<text x="{W/2}" y="28" text-anchor="middle" font-size="18" '
             f'font-weight="600" fill="#1a1a1a">{title}</text>')

    # y gridlines + labels
    for i in range(6):
        v = ymax * i / 5
        y = T + ph - ph * i / 5
        s.append(f'<line x1="{L}" y1="{y:.1f}" x2="{W-R}" y2="{y:.1f}" '
                 f'stroke="#e6e6e6"/>')
        s.append(f'<text x="{L-8}" y="{y+4:.1f}" text-anchor="end" '
                 f'font-size="11" fill="#666">{v:.0f}</text>')
    s.append(f'<text x="16" y="{T+ph/2}" text-anchor="middle" font-size="12" '
             f'fill="#444" transform="rotate(-90 16 {T+ph/2})">{ylabel}</text>')

    gw = pw / len(groups)
    bw = gw * 0.8 / len(series)
    lut = {(r["program"], r["predictor"]): r for r in rows}
    for gi, g in enumerate(groups):
        gx = L + gi * gw
        for si, ser in enumerate(series):
            r = lut.get((g, ser))
            if not r:
                continue
            val = float(r[value_key])
            bh = ph * min(val, ymax) / ymax
            x = gx + gw * 0.1 + si * bw
            y = T + ph - bh
            s.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{bw-1:.1f}" '
                     f'height="{bh:.1f}" fill="{PALETTE[si]}"/>')
        s.append(f'<text x="{gx+gw/2:.1f}" y="{T+ph+18}" text-anchor="middle" '
                 f'font-size="12" fill="#333">{g}</text>')

    # legend
    lx, ly = L, H - 40
    for si, ser in enumerate(series):
        x = lx + si * 145
        s.append(f'<rect x="{x}" y="{ly}" width="12" height="12" fill="{PALETTE[si]}"/>')
        s.append(f'<text x="{x+16}" y="{ly+11}" font-size="11" fill="#333">{ser}</text>')
    s.append("</svg>")
    with open(os.path.join(DOCS, fname), "w") as f:
        f.write("\n".join(s))


def line_chart(rows, program, xkey, ykey, title, xlabel, ylabel, fname):
    pts = [(int(r[xkey]), float(r[ykey])) for r in rows if r["program"] == program]
    pts.sort()
    W, H = 620, 380
    L, R, T, B = 60, 25, 50, 55
    pw, ph = W - L - R, H - T - B
    xs = [p[0] for p in pts]
    ymax = max(p[1] for p in pts) * 1.15 or 1
    s = svg_header(W, H)
    s.append(f'<text x="{W/2}" y="28" text-anchor="middle" font-size="18" '
             f'font-weight="600" fill="#1a1a1a">{title}</text>')
    for i in range(6):
        v = ymax * i / 5
        y = T + ph - ph * i / 5
        s.append(f'<line x1="{L}" y1="{y:.1f}" x2="{W-R}" y2="{y:.1f}" stroke="#e6e6e6"/>')
        s.append(f'<text x="{L-8}" y="{y+4:.1f}" text-anchor="end" font-size="11" '
                 f'fill="#666">{v:.0f}</text>')

    def X(v):  # log-ish even spacing by index
        idx = xs.index(v)
        return L + pw * idx / (len(xs) - 1)

    def Y(v):
        return T + ph - ph * v / ymax

    path = " ".join(f'{"M" if i==0 else "L"} {X(x):.1f} {Y(y):.1f}'
                    for i, (x, y) in enumerate(pts))
    s.append(f'<path d="{path}" fill="none" stroke="{PALETTE[4]}" stroke-width="2.5"/>')
    for x, y in pts:
        s.append(f'<circle cx="{X(x):.1f}" cy="{Y(y):.1f}" r="4" fill="{PALETTE[4]}"/>')
        s.append(f'<text x="{X(x):.1f}" y="{T+ph+18}" text-anchor="middle" '
                 f'font-size="11" fill="#333">{x}</text>')
    s.append(f'<text x="{W/2}" y="{H-8}" text-anchor="middle" font-size="12" '
             f'fill="#444">{xlabel}</text>')
    s.append(f'<text x="16" y="{T+ph/2}" text-anchor="middle" font-size="12" '
             f'fill="#444" transform="rotate(-90 16 {T+ph/2})">{ylabel}</text>')
    s.append("</svg>")
    with open(os.path.join(DOCS, fname), "w") as f:
        f.write("\n".join(s))


def main():
    os.makedirs(DOCS, exist_ok=True)
    preds = read_csv("predictors.csv")
    caches = read_csv("caches.csv")
    series = ["static-NT", "bimodal-1bit", "bimodal-2bit", "gshare", "tournament"]
    groups = ["nested_loops", "streaming", "bubble_sort", "gcd"]

    grouped_bars(preds, groups, series, "acc",
                 "Branch prediction accuracy by predictor", 100.0,
                 "accuracy (%)", "predictors_accuracy.svg")
    line_chart(caches, "streaming", "l1d_size", "l1d_miss",
               "L1-D miss rate vs cache size (streaming)",
               "L1-D size (bytes)", "miss rate (%)", "cache_cliff.svg")
    print("wrote docs/predictors_accuracy.svg and docs/cache_cliff.svg")


if __name__ == "__main__":
    main()
