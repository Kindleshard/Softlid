// Renders a loop (loops\<name>.html) frame by frame in headless Chrome and makes the README's animated WebP:
//   node render.mjs breaks            -> docs\media\breaks.webp   (and a preview MP4 in cpp\build\media)
//   node render.mjs breaks --at 2,7.5 -> just those moments, as cpp\build\media\peek\breaks-<t>.png
//   node render.mjs blink --clean     -> without its words, at 720x450 (for the README's grid)
// The page gives window.LOOP = {width, height, seconds} and window.seek(t), which draws the moment t (a promise).
// Chrome is driven through its DevTools protocol over Node's own WebSocket: nothing to install.
import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const cpp = resolve(here, '..', '..');
const name = process.argv[2];
const clean = process.argv.includes('--clean');
if (!name) { console.error('node render.mjs <loop name>'); process.exit(2); }
const fps = 30;
const ffmpeg = process.env.FFMPEG || 'C:\\Tools\\ffmpeg\\ffmpeg.exe';
const chrome = [`${process.env.ProgramFiles}\\Google\\Chrome\\Application\\chrome.exe`,
    `${process.env['ProgramFiles(x86)']}\\Google\\Chrome\\Application\\chrome.exe`,
    `${process.env.LOCALAPPDATA}\\Google\\Chrome\\Application\\chrome.exe`].find(existsSync);
const work = join(cpp, 'build', 'media');
const frames = join(work, 'frames', name);
rmSync(frames, { recursive: true, force: true });
mkdirSync(frames, { recursive: true });

// Headless Chrome with a profile of its own (one per run, so renders can run side by side).
const port = 9300 + Math.floor(Math.random() * 500);
const browser = spawn(chrome, ['--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${join(work, 'render-profiles', String(process.pid))}`,
    '--allow-file-access-from-files', '--hide-scrollbars', '--force-color-profile=srgb', '--disable-gpu-vsync', 'about:blank'],
    { stdio: 'ignore' });
let target;
for (let i = 0; i < 100 && !target; i++) {
    await new Promise(r => setTimeout(r, 100));
    try { target = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()).find(t => t.type === 'page'); } catch { }
}
if (!target) throw new Error('Chrome did not start');
const socket = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((ok, fail) => { socket.onopen = ok; socket.onerror = fail; });
let nextId = 0;
const waiting = new Map();
socket.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && waiting.has(m.id)) { const w = waiting.get(m.id); waiting.delete(m.id); m.error ? w.fail(new Error(m.error.message)) : w.ok(m.result); }
};
const send = (method, params = {}) => new Promise((ok, fail) => {
    const id = ++nextId;
    waiting.set(id, { ok, fail });
    socket.send(JSON.stringify({ id, method, params }));
});
const evaluate = async expression => {
    const r = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text);
    return r.result.value;
};

await send('Page.enable');
const loaded = new Promise(ok => { const old = socket.onmessage; socket.onmessage = e => { old(e); if (JSON.parse(e.data).method === 'Page.loadEventFired') ok(); }; });
await send('Page.navigate', { url: pathToFileURL(join(here, 'loops', `${name}.html`)).href + (clean ? '?clean' : '') });
await loaded;
const loop = await evaluate('document.fonts.ready.then(() => window.LOOP)');
await send('Emulation.setDeviceMetricsOverride', { width: loop.width, height: loop.height, deviceScaleFactor: clean ? 0.75 : 1, mobile: false });

// The footage it uses, unpacked once from the recordings into 60 fps frames, 1280 px wide unless the loop asks for
// more (to zoom in close); the loop picks the frames it needs.
for (const f of loop.footage ?? []) {
    const dir = join(work, 'footage', f.name);
    if (existsSync(join(dir, '00001.jpg'))) continue;
    mkdirSync(dir, { recursive: true });
    spawnSync(ffmpeg, ['-v', 'error', '-i', join(work, 'raw', `${f.name}.mp4`), '-t', String(f.to + 0.1), '-vf', `fps=60,scale=${f.width ?? 1280}:-1:flags=lanczos`,
        '-q:v', '3', join(dir, '%05d.jpg')], { stdio: 'inherit' });
}

// Just some moments, to look at.
const at = process.argv.indexOf('--at');
if (at > 0) {
    const peek = join(work, 'peek');
    mkdirSync(peek, { recursive: true });
    for (const t of process.argv[at + 1].split(',').map(Number)) {
        await evaluate(`window.seek(${t})`);
        const shot = await send('Page.captureScreenshot', { format: 'png' });
        writeFileSync(join(peek, `${name}-${t}.png`), Buffer.from(shot.data, 'base64'));
        console.log(join(peek, `${name}-${t}.png`));
    }
    socket.close();
    browser.kill();
    process.exit(0);
}

const count = Math.round(loop.seconds * fps);
for (let i = 0; i < count; i++) {
    await evaluate(`window.seek(${i / fps})`);
    const shot = await send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false });
    writeFileSync(join(frames, `${String(i).padStart(5, '0')}.png`), Buffer.from(shot.data, 'base64'));
    if (i % fps === 0) process.stdout.write(`\r${name}: ${i}/${count} frames`);
}
process.stdout.write(`\r${name}: ${count}/${count} frames\n`);
socket.close();
browser.kill();

// The loop for the README (animated WebP), and an MP4 to look at it frame by frame.
const docs = join(cpp, '..', 'docs', 'media');
mkdirSync(docs, { recursive: true });
const input = ['-v', 'error', '-y', '-framerate', String(fps), '-i', join(frames, '%05d.png')];
const webp = join(docs, `${name}.webp`);
spawnSync(ffmpeg, [...input, '-c:v', 'libwebp_anim', '-lossless', '0', '-quality', String(loop.quality ?? 72),
    '-compression_level', '6', '-loop', '0', webp], { stdio: 'inherit' });
spawnSync(ffmpeg, [...input, '-c:v', 'libx264', '-crf', '16', '-pix_fmt', 'yuv420p', join(work, `${name}.mp4`)], { stdio: 'inherit' });
const { size } = await import('node:fs').then(fs => fs.statSync(webp));
console.log(`${webp}  ${Math.round(size / 1024)} KB`);
