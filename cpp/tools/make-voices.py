# Speaks the guided breaks' and the tour's lines with Chatterbox Multilingual (Resemble AI, MIT), once, into small mono
# MP3s in res\voice. It runs on the graphics card, in a Python that has Chatterbox from its GitHub repository:
#   uv venv --python 3.11 E:\AI\tts-lab\chatterbox
#   uv pip install --python E:\AI\tts-lab\chatterbox\Scripts\python.exe --extra-index-url https://download.pytorch.org/whl/cu126 git+https://github.com/resemble-ai/chatterbox
# then, with HF_HOME pointing where the model may be downloaded to (about 3 GB):
#   make-voices.py                     every language Chatterbox speaks
#   make-voices.py tr en               just these
#   make-voices.py --only far,done tr  just these lines
# Every language is read by Chatterbox's own voice, so BlinkBuddy sounds like one person in all of them. Chatterbox
# doesn't speak Czech, Hungarian, Romanian or Ukrainian: their folders keep the Piper clips made before.
# Each clip: silence trimmed, a little quiet at both ends, loudness evened out, 24 kHz mono MP3 at 32 kbit/s.
import os, pathlib, subprocess, sys, tempfile

CPP = pathlib.Path(__file__).resolve().parent.parent
FFMPEG = os.environ.get('FFMPEG', r'C:\Tools\ffmpeg\ffmpeg.exe')

# The English text of each line, as the program passes it to Lang::T (the translations are found by it in res\lang).
LINES = {
    'far': "Look away from the screen, at something far away, and let your eyes soften.",
    'sides': "Slowly look left and right, keeping your head still.",
    'updown': "Now slowly look up and down.",
    'circle': "Roll your eyes in a slow, wide circle.",
    'close': "Close your eyes gently, then blink slowly a few times.",
    'done': "Well done. You can look back now.",
    'stretch_intro': "Time to stand up. Follow along with a few easy stretches.",
    'stretch_reach': "Reach both arms up high, and stretch tall.",
    'stretch_shoulders': "Roll your shoulders slowly backwards.",
    'stretch_neck': "Gently tilt your head toward each shoulder.",
    'stretch_twist': "Twist your upper body slowly, side to side.",
    'stretch_chest': "Open your arms wide, and squeeze your shoulder blades together.",
    'stretch_march': "March in place, lifting your knees.",
    'stretch_calves': "Rise up onto your toes, then lower slowly.",
    'stretch_fold': "Bend your knees a little, and hang forward to stretch your back.",
    'stretch_done': "Great job. Have a sip of water, and back to it.",
    'tour_welcome': "Welcome to BlinkBuddy. Gentle breaks and a calmer screen while you work. This takes about a minute.",
    'tour_pick': "Pick what helps you. You can change all of it later.",
    'tour_tray': "BlinkBuddy waits in the tray by the clock. Click it for settings, right-click for quick actions like pausing.",
    'tour_done': "You're all set. Want to see what a break looks like? Try one now, or finish the tour.",
}
# The eye break's lines must fit a 20 s break, so they have less quiet around them.
LOOKAWAY = {'far', 'sides', 'updown', 'circle', 'close', 'done'}

# Takes other than the first that were kept: a more natural pace, or (Finnish) an eye break that fits its 20 s.
SEEDS = {('en', 'updown'): 3, ('zh', 'done'): 3, ('fi', 'far'): 3, ('fi', 'sides'): 2, ('fi', 'updown'): 2,
         ('fi', 'circle'): 4, ('fi', 'close'): 4, ('fi', 'done'): 2}

LANGUAGES = ['da', 'de', 'el', 'en', 'es', 'fi', 'fr', 'it', 'ja', 'ko', 'nl', 'pl', 'pt', 'ru', 'sv', 'tr', 'zh']


def texts(code):
    def read(c):
        raw = (CPP / 'res' / 'lang' / f'{c}.txt').read_bytes().removeprefix(b'\xef\xbb\xbf')
        return raw.decode('utf-8').replace('\r\n', '\n').split('\n')
    en, other = read('en'), read(code)
    out = {}
    for key, line in LINES.items():
        if line not in en: sys.exit(f'not in en.txt: {line!r}')
        out[key] = other[en.index(line)]
    return out


def encode(src, dst, pad):
    trim = ('silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.05,areverse,'
            'silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.05,areverse,'
            f'adelay={int(pad * 1000)}|{int(pad * 1000)},apad=pad_dur={pad},loudnorm=I=-18:TP=-2:LRA=11')
    subprocess.run([FFMPEG, '-v', 'error', '-y', '-i', src, '-af', trim, '-ar', '24000', '-ac', '1', '-c:a', 'libmp3lame',
                    '-b:a', '32k', '-map_metadata', '-1', '-id3v2_version', '0', '-write_id3v1', '0', str(dst)], check=True)


def seconds(mp3):
    out = subprocess.run([FFMPEG, '-i', str(mp3)], capture_output=True, text=True).stderr
    h, m, s = out.split('Duration: ')[1].split(',')[0].split(':')
    return int(h) * 3600 + int(m) * 60 + float(s)


def main():
    import torch, torchaudio
    from chatterbox.mtl_tts import ChatterboxMultilingualTTS
    args = sys.argv[1:]
    only = None
    if '--only' in args:
        i = args.index('--only'); only = set(args[i + 1].split(',')); del args[i:i + 2]
    model = ChatterboxMultilingualTTS.from_pretrained(device='cuda', t3_model='v3')
    for code in args or LANGUAGES:
        out = CPP / 'res' / 'voice' / code
        out.mkdir(parents=True, exist_ok=True)
        for key, text in texts(code).items():
            if only and key not in only: continue
            torch.manual_seed(SEEDS.get((code, key), 1))   # (the same take each time it's run)
            wav = model.generate(text, language_id=code)
            with tempfile.TemporaryDirectory() as tmp:
                raw = os.path.join(tmp, 'a.wav')
                torchaudio.save(raw, wav, model.sr)
                encode(raw, out / f'{key}.mp3', 0.08 if key in LOOKAWAY else 0.15)
            print(f'  {code} {key} {seconds(out / f"{key}.mp3"):.1f} s', flush=True)
        if all((out / f'{k}.mp3').exists() for k in LOOKAWAY):
            eyes = sum(seconds(out / f'{k}.mp3') for k in LOOKAWAY)
            print(f'{code} done: the eye break speaks for {eyes:.1f} s', flush=True)


if __name__ == '__main__':
    main()
