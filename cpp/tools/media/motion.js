// What the loops (loops\*.html) share: easing, timing, the filmed footage, and words that slide in.
// A loop sets window.LOOP and window.seek(t); render.mjs calls seek for each frame and takes a screenshot.
const M = {
    clamp: (x, a = 0, b = 1) => Math.min(b, Math.max(a, x)),
    mix: (a, b, k) => a + (b - a) * k,
    // How far t is through [a, b], 0..1.
    span(t, a, b) { return M.clamp((t - a) / (b - a)); },
    smooth: x => x * x * (3 - 2 * x),
    out: x => 1 - Math.pow(1 - x, 3),
    inOut: x => x < 0.5 ? 4 * x * x * x : 1 - Math.pow(-2 * x + 2, 3) / 2,
    // A value along key times: [[t0, v0], [t1, v1], ...], eased between them.
    keys(t, list, ease = M.inOut) {
        if (t <= list[0][0]) return list[0][1];
        for (let i = 1; i < list.length; i++)
            if (t <= list[i][0]) return M.mix(list[i - 1][1], list[i][1], ease(M.span(t, list[i - 1][0], list[i][0])));
        return list[list.length - 1][1];
    },
};

// Filmed footage (record.ps1), as 60 fps frames that render.mjs unpacks to cpp\build\media\footage\<name>.
// Two layers, so one shot can fade into the next.
class Footage {
    constructor(parent) {
        this.layers = [0, 1].map(() => {
            const img = document.createElement('img');
            img.className = 'footage';
            parent.appendChild(img);
            return img;
        });
    }
    static src(name, raw) { return `../../../build/media/footage/${name}/${String(Math.max(1, Math.round(raw * 60) + 1)).padStart(5, '0')}.jpg`; }
    // shots: [{name, from, to, at, until, fade, zoom, focus}] - the footage from..to played over at..until (any speed);
    // a shot fades in over the one before it for `fade` seconds. zoom: a scale, or [start, end] to push in; focus:
    // [x, y] (0..1) of the footage, brought as near the middle as the zoom allows without showing past an edge.
    async show(t, shots) {
        let i = shots.findIndex(s => t < s.until);
        if (i < 0) i = shots.length - 1;
        const s = shots[i], prev = shots[i - 1];
        const k = s2 => M.clamp((t - s2.at) / (s2.until - s2.at));
        const raw = s2 => M.mix(s2.from, s2.to, k(s2));
        const frame = (img, s2) => {
            img.src = Footage.src(s2.name, raw(s2));
            const z = Array.isArray(s2.zoom) ? M.mix(s2.zoom[0], s2.zoom[1], M.inOut(k(s2))) : (s2.zoom ?? 1);
            const [fx, fy] = s2.focus ?? [0.5, 0.5];
            const cx = M.clamp(fx, 0.5 / z, 1 - 0.5 / z), cy = M.clamp(fy, 0.5 / z, 1 - 0.5 / z);
            img.style.transformOrigin = '0 0';
            img.style.transform = `translate(${(0.5 - cx * z) * 100}%, ${(0.5 - cy * z) * 100}%) scale(${z})`;
        };
        const fading = prev && s.fade && t < s.at + s.fade;
        const [top, under] = this.layers;
        frame(top, s);
        top.style.opacity = fading ? M.smooth(M.span(t, s.at, s.at + s.fade)) : 1;
        under.style.opacity = fading ? 1 : 0;
        if (fading) frame(under, prev);
        top.style.zIndex = 2;
        under.style.zIndex = 1;
        await Promise.all(this.layers.map(img => img.decode().catch(() => { })));
    }
}

// A line of words that slide up into place one after another, and out again.
class Words {
    constructor(el) {
        this.el = el;
        el.innerHTML = el.textContent.trim().split(/\s+/).map(w => `<span class="w"><span>${w}</span></span>`).join(' ');
        this.words = [...el.querySelectorAll('.w > span')];
    }
    // in: 0..1 how far in, out: 0..1 how far gone
    set(inK, outK = 0, stagger = 0.08) {
        const n = this.words.length;
        this.words.forEach((w, i) => {
            const k = M.out(M.clamp((inK * (1 + stagger * n) - stagger * i)));
            const g = M.inOut(M.clamp(outK * (1 + stagger * n * 0.5) - stagger * 0.5 * i));
            w.style.transform = `translateY(${(1 - k) * 110 - g * 110}%)`;
            w.style.opacity = Math.min(k, 1 - g);
        });
    }
}
